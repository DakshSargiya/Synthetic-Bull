import { useEffect, useRef, useCallback, useState } from 'react';
import type {
  CandleData, OrderBookSnapshot, PortfolioState, TradeEvent, OpenOrder,
} from '../types';

// VITE_WS_URL can be:
//   - a full URL, e.g. "ws://localhost:9001"           (plain dev backend)
//   - a path, e.g. "/ws"                                (nginx-proxied prod build —
//     see frontend/nginx.conf and docker-compose.yml, which proxy that path
//     to the "backend" container so the browser never needs a direct route
//     to :9001)
// A bare path is resolved against the page's own origin so the same build
// works whether it's served over http or https.
const RAW_WS_URL = (import.meta as any).env?.VITE_WS_URL ?? 'ws://localhost:9001';
const WS_URL = RAW_WS_URL.startsWith('/')
  ? `${window.location.protocol === 'https:' ? 'wss' : 'ws'}://${window.location.host}${RAW_WS_URL}`
  : RAW_WS_URL;

export interface ExchangeState {
  connected: boolean;
  clientId: string;
  candles: CandleData[];
  snapshot: OrderBookSnapshot | null;
  portfolio: PortfolioState;
  recentTrades: TradeEvent[];
  openOrders: OpenOrder[];
  lastError: string | null;
  lastPrice: number;
}

const DEFAULT_PORTFOLIO: PortfolioState = {
  cash: 100000, holdings: 0, avgCost: 0,
  realizedPnL: 0, unrealizedPnL: 0, totalValue: 100000,
};

export function useExchange() {
  const wsRef  = useRef<WebSocket | null>(null);
  const [state, setState] = useState<ExchangeState>({
    connected: false,
    clientId: '',
    candles: [],
    snapshot: null,
    portfolio: DEFAULT_PORTFOLIO,
    recentTrades: [],
    openOrders: [],
    lastError: null,
    lastPrice: 50000,
  });

  // -----------------------------------------------------------------------
  // Connect / reconnect
  // -----------------------------------------------------------------------
  useEffect(() => {
    let ws: WebSocket;
    let reconnectTimer: ReturnType<typeof setTimeout>;

    function connect() {
      ws = new WebSocket(WS_URL);
      wsRef.current = ws;

      ws.onopen = () => {
        setState(s => ({ ...s, connected: true }));
      };

      ws.onclose = () => {
        // A fresh reconnect gets a brand-new connId/portfolio server-side,
        // so any orders we thought were open are gone with it.
        setState(s => ({ ...s, connected: false, openOrders: [] }));
        reconnectTimer = setTimeout(connect, 2000);
      };

      ws.onerror = () => ws.close();

      ws.onmessage = (ev) => {
        try {
          const msg = JSON.parse(ev.data as string);
          handleMessage(msg);
        } catch {}
      };
    }

    connect();
    return () => {
      clearTimeout(reconnectTimer);
      ws?.close();
    };
  }, []); // eslint-disable-line

  // -----------------------------------------------------------------------
  // Message router
  // -----------------------------------------------------------------------
  function handleMessage(msg: any) {
    switch (msg.type) {
      case 'init': {
        const hist: CandleData[] = (msg.history?.candles ?? []).map(parseCandle);
        setState(s => ({
          ...s,
          clientId: msg.clientId,
          candles: hist,
          portfolio: msg.portfolio ?? s.portfolio,
          openOrders: [], // new connection → server-side portfolio/book state is fresh
        }));
        break;
      }
      case 'snapshot': {
        const snap: OrderBookSnapshot = {
          bids: msg.bids ?? [],
          asks: msg.asks ?? [],
          mid: msg.mid ?? 0,
          ts: msg.ts ?? Date.now(),
        };
        setState(s => ({
          ...s,
          snapshot: snap,
          lastPrice: snap.mid || s.lastPrice,
        }));
        break;
      }
      case 'candle': {
        const c = parseCandle(msg);
        setState(s => {
          const candles = upsertCandle(s.candles, c);
          return { ...s, candles, lastPrice: c.close || s.lastPrice };
        });
        break;
      }
      case 'candle_history': {
        const hist: CandleData[] = (msg.candles ?? []).map(parseCandle);
        setState(s => ({ ...s, candles: hist }));
        break;
      }
      case 'trade': {
        const t: TradeEvent = {
          id: msg.id,
          buyOrderId: msg.buyOrderId, sellOrderId: msg.sellOrderId,
          price: msg.price, qty: msg.qty,
          buyClient: msg.buyClient, sellClient: msg.sellClient, ts: msg.ts,
        };
        setState(s => ({
          ...s,
          recentTrades: [t, ...s.recentTrades].slice(0, 50),
          lastPrice: t.price,
          // A fill on one of our resting orders shrinks (or clears) it —
          // the authoritative remainingQty still arrives via order_ack, but
          // this keeps the list from visibly lagging between now and then.
          openOrders: s.openOrders
            .map(o => {
              if (o.orderId === t.buyOrderId || o.orderId === t.sellOrderId) {
                return { ...o, remainingQty: Math.max(0, o.remainingQty - t.qty) };
              }
              return o;
            })
            .filter(o => o.remainingQty > 1e-9),
        }));
        break;
      }
      case 'order_ack': {
        const ack: OpenOrder = {
          orderId: msg.orderId, side: msg.side, orderType: msg.orderType,
          price: msg.price, qty: msg.qty, remainingQty: msg.remainingQty,
        };
        setState(s => {
          const withoutThis = s.openOrders.filter(o => o.orderId !== ack.orderId);
          // Only LIMIT orders can rest; a fully-filled or market order has
          // nothing left to show as "open".
          const stillOpen = ack.orderType === 'LIMIT' && ack.remainingQty > 1e-9;
          return {
            ...s,
            openOrders: stillOpen ? [...withoutThis, ack] : withoutThis,
          };
        });
        break;
      }
      case 'order_cancelled': {
        setState(s => ({
          ...s,
          openOrders: s.openOrders.filter(o => o.orderId !== msg.orderId),
        }));
        break;
      }
      case 'error': {
        setState(s => ({ ...s, lastError: msg.message ?? 'unknown error' }));
        break;
      }
      case 'portfolio': {
        setState(s => ({
          ...s,
          portfolio: {
            cash: msg.cash, holdings: msg.holdings, avgCost: msg.avgCost,
            realizedPnL: msg.realizedPnL, unrealizedPnL: msg.unrealizedPnL,
            totalValue: msg.totalValue,
          },
        }));
        break;
      }
      default: break;
    }
  }

  // -----------------------------------------------------------------------
  // Order submission
  // -----------------------------------------------------------------------
  const submitLimitOrder = useCallback((side: 'BID' | 'ASK', price: number, qty: number) => {
    wsRef.current?.send(JSON.stringify({ type: 'limit_order', side, price, qty }));
  }, []);

  const submitMarketOrder = useCallback((side: 'BID' | 'ASK', qty: number) => {
    wsRef.current?.send(JSON.stringify({ type: 'market_order', side, qty }));
  }, []);

  const cancelOrder = useCallback((orderId: number) => {
    wsRef.current?.send(JSON.stringify({ type: 'cancel_order', orderId }));
  }, []);

  const clearError = useCallback(() => {
    setState(s => ({ ...s, lastError: null }));
  }, []);

  return { state, submitLimitOrder, submitMarketOrder, cancelOrder, clearError };
}

// ---------- helpers ----------

function parseCandle(msg: any): CandleData {
  return {
    time: msg.time, open: msg.open, high: msg.high,
    low: msg.low, close: msg.close, volume: msg.volume,
    live: msg.live,
  };
}

function upsertCandle(candles: CandleData[], c: CandleData): CandleData[] {
  const arr = [...candles];
  const idx = arr.findIndex(x => x.time === c.time);
  if (idx >= 0) arr[idx] = c;
  else arr.push(c);
  // keep last 300
  if (arr.length > 300) arr.shift();
  return arr;
}

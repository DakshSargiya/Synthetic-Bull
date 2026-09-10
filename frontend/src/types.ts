export interface CandleData {
  time: number;   // Unix seconds
  open: number;
  high: number;
  low: number;
  close: number;
  volume: number;
  live?: boolean;
}

export interface OrderBookLevel {
  price: number;
  qty: number;
}

export interface OrderBookSnapshot {
  bids: [number, number][];   // [price, qty]
  asks: [number, number][];
  mid: number;
  ts: number;
}

export interface PortfolioState {
  cash: number;
  holdings: number;
  avgCost: number;
  realizedPnL: number;
  unrealizedPnL: number;
  totalValue: number;
}

export interface TradeEvent {
  id: number;
  buyOrderId?: number;
  sellOrderId?: number;
  price: number;
  qty: number;
  buyClient: string;
  sellClient: string;
  ts: number;
}

export type OrderSide = 'BID' | 'ASK';

export interface PendingOrder {
  id: number;
  side: OrderSide;
  type: 'LIMIT' | 'MARKET';
  price?: number;
  qty: number;
}

// One of the client's own resting orders, tracked from order_ack /
// order_cancelled / trade events so the UI can offer a cancel button.
export interface OpenOrder {
  orderId: number;
  side: OrderSide;
  orderType: 'LIMIT' | 'MARKET';
  price: number;
  qty: number;
  remainingQty: number;
}

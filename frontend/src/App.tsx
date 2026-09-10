import { useExchange } from './hooks/useExchange';
import { CandlestickChart } from './components/CandlestickChart';
import { OrderBookDepth }   from './components/OrderBookDepth';
import { OrderPanel }       from './components/OrderPanel';
import { PortfolioWidget }  from './components/PortfolioWidget';
import { TradeTicker }      from './components/TradeTicker';
import { OpenOrders }       from './components/OpenOrders';

export default function App() {
  const { state, submitLimitOrder, submitMarketOrder, cancelOrder, clearError } = useExchange();
  const { connected, clientId, candles, snapshot, portfolio, recentTrades, openOrders, lastError, lastPrice } = state;

  return (
    <div style={layout.root}>
      {/* ── Top bar ── */}
      <header style={layout.topbar}>
        <div style={layout.logo}>
          <span style={{ color: '#7b61ff', fontWeight: 800, fontSize: 18 }}>SYNTHETIC</span>
          <span style={{ color: '#c2c4dc', fontWeight: 300, fontSize: 18, marginLeft: 4 }}>BULL</span>
          <span style={{ color: '#4a4d6e', fontSize: 11, marginLeft: 10 }}>NextBull × IIT Kharagpur</span>
        </div>

        <div style={layout.topStats}>
          <TopStat label="PRICE"    value={`$${lastPrice.toLocaleString('en-US', { minimumFractionDigits: 2, maximumFractionDigits: 2 })}`} color="#c2c4dc" />
          {/* Not a true 24h figure — only the trades still held in the
              client-side ticker buffer (last 50). Labelled honestly instead
              of implying a rolling day window nothing here actually tracks. */}
          <TopStat label={`VOL (LAST ${recentTrades.length})`} value={`${recentTrades.reduce((s, t) => s + t.qty, 0).toFixed(2)}`} />
          <TopStat label="TRADES"   value={String(recentTrades.length)} />
          <TopStat label="MID"      value={snapshot ? `$${snapshot.mid.toLocaleString('en-US', { minimumFractionDigits: 2 })}` : '—'} />
        </div>

        <div style={{ display: 'flex', alignItems: 'center', gap: 8 }}>
          <div style={{
            width: 8, height: 8, borderRadius: '50%',
            background: connected ? '#00c853' : '#ff3d71',
            boxShadow: connected ? '0 0 6px #00c853' : 'none',
          }} />
          <span style={{ fontSize: 11, color: connected ? '#00c853' : '#ff3d71' }}>
            {connected ? 'LIVE' : 'RECONNECTING'}
          </span>
        </div>
      </header>

      {/* ── Error toast (rejected orders, cancels that failed, rate limit) ── */}
      {lastError && (
        <div style={layout.errorToast} onClick={clearError} title="Click to dismiss">
          {lastError}
        </div>
      )}

      {/* ── Main grid ── */}
      <main style={layout.main}>

        {/* Column 1: Chart + Trade ticker */}
        <div style={{ ...layout.col, gridArea: 'chart', gap: 8, display: 'flex', flexDirection: 'column' }}>
          <div style={{ flex: '1 1 0', minHeight: 0 }}>
            <div style={{ ...panelStyle, height: '100%' }}>
              <div style={panelHeader}>
                SYNTH-USD &nbsp;<span style={{ color: '#4a4d6e' }}>1s candles</span>
              </div>
              <div style={{ flex: 1, minHeight: 0 }}>
                <CandlestickChart candles={candles} lastPrice={lastPrice} />
              </div>
            </div>
          </div>
          <TradeTicker trades={recentTrades} clientId={clientId} />
        </div>

        {/* Column 2: Order book */}
        <div style={{ gridArea: 'book' }}>
          <OrderBookDepth snapshot={snapshot} lastPrice={lastPrice} />
        </div>

        {/* Column 3: Order panel + Portfolio */}
        <div style={{ gridArea: 'panel', display: 'flex', flexDirection: 'column', gap: 8, minHeight: 0, overflowY: 'auto' }}>
          <PortfolioWidget portfolio={portfolio} clientId={clientId} />
          <OrderPanel
            lastPrice={lastPrice}
            onLimit={submitLimitOrder}
            onMarket={submitMarketOrder}
          />
          <OpenOrders orders={openOrders} onCancel={cancelOrder} />
        </div>

      </main>
    </div>
  );
}

function TopStat({ label, value, color }: { label: string; value: string; color?: string }) {
  return (
    <div style={{ display: 'flex', flexDirection: 'column', alignItems: 'center' }}>
      <span style={{ fontSize: 9, color: '#4a4d6e', letterSpacing: '0.1em', textTransform: 'uppercase' }}>{label}</span>
      <span style={{ fontSize: 14, fontFamily: 'JetBrains Mono, monospace', color: color ?? '#9294ad', fontWeight: 500 }}>{value}</span>
    </div>
  );
}

const panelStyle: React.CSSProperties = {
  background: '#0d0e21',
  border: '1px solid #1c1e36',
  borderRadius: 10,
  display: 'flex',
  flexDirection: 'column',
  overflow: 'hidden',
};

const panelHeader: React.CSSProperties = {
  padding: '10px 14px',
  borderBottom: '1px solid #1c1e36',
  fontSize: 12,
  fontWeight: 600,
  color: '#c2c4dc',
  fontFamily: 'Inter, sans-serif',
};

const layout = {
  root: {
    display: 'flex',
    flexDirection: 'column' as const,
    height: '100vh',
    background: '#080916',
    color: '#c2c4dc',
    overflow: 'hidden',
    position: 'relative' as const,
  },
  topbar: {
    display: 'flex',
    alignItems: 'center',
    justifyContent: 'space-between',
    padding: '0 20px',
    height: 52,
    background: '#0d0e21',
    borderBottom: '1px solid #1c1e36',
    flexShrink: 0,
  },
  logo: {
    display: 'flex',
    alignItems: 'baseline',
    gap: 0,
    fontFamily: 'Inter, sans-serif',
  },
  topStats: {
    display: 'flex',
    gap: 28,
    alignItems: 'center',
  },
  errorToast: {
    position: 'absolute' as const,
    top: 60,
    right: 20,
    zIndex: 10,
    background: '#2a0d16',
    border: '1px solid #ff3d71',
    color: '#ff98b3',
    borderRadius: 8,
    padding: '10px 14px',
    fontSize: 12,
    fontFamily: 'Inter, sans-serif',
    cursor: 'pointer',
    maxWidth: 320,
    boxShadow: '0 4px 16px rgba(0,0,0,0.4)',
  },
  main: {
    flex: 1,
    minHeight: 0,
    display: 'grid',
    gridTemplateColumns: '1fr 260px 300px',
    gridTemplateRows: '1fr',
    gridTemplateAreas: '"chart book panel"',
    gap: 8,
    padding: 8,
    overflow: 'hidden',
  },
  col: {
    minHeight: 0,
    overflow: 'hidden',
  },
};

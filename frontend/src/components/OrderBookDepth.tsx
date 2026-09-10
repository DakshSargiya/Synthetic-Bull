import type { OrderBookSnapshot } from '../types';

interface Props {
  snapshot: OrderBookSnapshot | null;
  lastPrice: number;
}

const fmt = (n: number) => n.toLocaleString('en-US', { minimumFractionDigits: 2, maximumFractionDigits: 2 });
const fmtQ = (n: number) => n.toFixed(4);

export function OrderBookDepth({ snapshot, lastPrice }: Props) {
  if (!snapshot) {
    return (
      <div style={styles.container}>
        <div style={styles.header}>Order Book</div>
        <div style={styles.empty}>Connecting…</div>
      </div>
    );
  }

  const asks = [...snapshot.asks].slice(0, 14).reverse(); // ascending → show lowest ask at bottom
  const bids = snapshot.bids.slice(0, 14);

  // compute cumulative for depth bars
  const maxAskCum = asks.reduce((s, [, q]) => s + q, 0) || 1;
  const maxBidCum = bids.reduce((s, [, q]) => s + q, 0) || 1;
  let askCum = 0, bidCum = 0;

  const priceChange = lastPrice - (snapshot.mid || lastPrice);
  const pctChange = snapshot.mid ? (priceChange / snapshot.mid) * 100 : 0;

  return (
    <div style={styles.container}>
      {/* Header */}
      <div style={styles.header}>
        <span>Order Book</span>
        <span style={{ color: pctChange >= 0 ? '#00c853' : '#ff3d71', fontFamily: 'JetBrains Mono, monospace', fontSize: 13 }}>
          ${fmt(lastPrice)}
          <span style={{ fontSize: 11, marginLeft: 6, color: '#9294ad' }}>
            {pctChange >= 0 ? '+' : ''}{pctChange.toFixed(3)}%
          </span>
        </span>
      </div>

      {/* Column labels */}
      <div style={styles.colRow}>
        <span>Price (USD)</span>
        <span>Qty</span>
        <span>Total</span>
      </div>

      {/* Asks (sells) — reversed so lowest ask is closest to mid */}
      <div>
        {asks.map(([price, qty], i) => {
          askCum += qty;
          const pct = (askCum / maxAskCum) * 100;
          return (
            <div key={i} style={{ ...styles.row, position: 'relative' }}>
              <div style={{ ...styles.depthBar, width: `${pct}%`, background: 'rgba(255,61,113,0.12)' }} />
              <span style={{ ...styles.cell, color: '#ff3d71' }}>{fmt(price)}</span>
              <span style={{ ...styles.cell, color: '#9294ad' }}>{fmtQ(qty)}</span>
              <span style={{ ...styles.cell, color: '#9294ad' }}>{fmtQ(askCum)}</span>
            </div>
          );
        })}
      </div>

      {/* Mid price banner */}
      <div style={styles.midBanner}>
        <span style={{ color: '#7b61ff', fontFamily: 'JetBrains Mono, monospace', fontSize: 14, fontWeight: 600 }}>
          ${fmt(snapshot.mid)}
        </span>
        <span style={{ color: '#9294ad', fontSize: 11, marginLeft: 8 }}>mid</span>
      </div>

      {/* Bids (buys) */}
      <div>
        {bids.map(([price, qty], i) => {
          bidCum += qty;
          const pct = (bidCum / maxBidCum) * 100;
          return (
            <div key={i} style={{ ...styles.row, position: 'relative' }}>
              <div style={{ ...styles.depthBar, width: `${pct}%`, background: 'rgba(0,200,83,0.10)' }} />
              <span style={{ ...styles.cell, color: '#00c853' }}>{fmt(price)}</span>
              <span style={{ ...styles.cell, color: '#9294ad' }}>{fmtQ(qty)}</span>
              <span style={{ ...styles.cell, color: '#9294ad' }}>{fmtQ(bidCum)}</span>
            </div>
          );
        })}
      </div>
    </div>
  );
}

const styles: Record<string, React.CSSProperties> = {
  container: {
    background: '#0d0e21',
    borderRadius: 10,
    border: '1px solid #1c1e36',
    display: 'flex',
    flexDirection: 'column',
    overflow: 'hidden',
    height: '100%',
    fontFamily: 'JetBrains Mono, monospace',
    fontSize: 12,
  },
  header: {
    display: 'flex',
    justifyContent: 'space-between',
    alignItems: 'center',
    padding: '10px 14px',
    borderBottom: '1px solid #1c1e36',
    color: '#c2c4dc',
    fontFamily: 'Inter, sans-serif',
    fontWeight: 600,
    fontSize: 13,
  },
  colRow: {
    display: 'grid',
    gridTemplateColumns: '1fr 1fr 1fr',
    padding: '4px 14px',
    color: '#4a4d6e',
    fontSize: 11,
    fontFamily: 'Inter, sans-serif',
    borderBottom: '1px solid #12132a',
  },
  row: {
    display: 'grid',
    gridTemplateColumns: '1fr 1fr 1fr',
    padding: '3px 14px',
    overflow: 'hidden',
  },
  depthBar: {
    position: 'absolute',
    right: 0,
    top: 0,
    height: '100%',
    pointerEvents: 'none',
  },
  cell: {
    position: 'relative',
    zIndex: 1,
    fontSize: 12,
  },
  midBanner: {
    padding: '6px 14px',
    borderTop: '1px solid #1c1e36',
    borderBottom: '1px solid #1c1e36',
    background: '#12132a',
  },
  empty: {
    color: '#4a4d6e',
    textAlign: 'center',
    padding: 24,
    fontFamily: 'Inter, sans-serif',
  },
};

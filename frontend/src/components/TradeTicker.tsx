import type { TradeEvent } from '../types';

interface Props {
  trades: TradeEvent[];
  clientId: string;
}

const fmt = (n: number) => n.toLocaleString('en-US', { minimumFractionDigits: 2, maximumFractionDigits: 2 });

export function TradeTicker({ trades, clientId }: Props) {
  return (
    <div style={styles.container}>
      <div style={styles.header}>Recent Trades</div>
      <div style={styles.colRow}>
        <span>Price</span>
        <span>Qty</span>
        <span>Time</span>
      </div>
      <div style={{ overflowY: 'auto', maxHeight: 220 }}>
        {trades.length === 0 && (
          <div style={styles.empty}>Waiting for trades…</div>
        )}
        {trades.map((t, i) => {
          const mine = t.buyClient === clientId || t.sellClient === clientId;
          const ts   = new Date(t.ts).toLocaleTimeString('en-US', { hour12: false, hour: '2-digit', minute: '2-digit', second: '2-digit' });
          return (
            <div key={t.id} style={{ ...styles.row, ...(mine ? styles.mineRow : {}), ...(i === 0 ? styles.newRow : {}) }}>
              <span style={{ color: '#00c853', fontFamily: 'JetBrains Mono, monospace', fontSize: 12 }}>{fmt(t.price)}</span>
              <span style={{ color: '#9294ad', fontFamily: 'JetBrains Mono, monospace', fontSize: 12 }}>{t.qty.toFixed(4)}</span>
              <span style={{ color: '#4a4d6e', fontSize: 11 }}>{ts}</span>
              {mine && <span style={styles.youTag}>YOU</span>}
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
    fontFamily: 'Inter, sans-serif',
    overflow: 'hidden',
  },
  header: {
    padding: '12px 14px',
    borderBottom: '1px solid #1c1e36',
    fontWeight: 600,
    fontSize: 13,
    color: '#c2c4dc',
  },
  colRow: {
    display: 'grid',
    gridTemplateColumns: '1fr 1fr 1fr 40px',
    padding: '4px 14px',
    color: '#4a4d6e',
    fontSize: 11,
    borderBottom: '1px solid #12132a',
  },
  row: {
    display: 'grid',
    gridTemplateColumns: '1fr 1fr 1fr 40px',
    alignItems: 'center',
    padding: '5px 14px',
    borderBottom: '1px solid #0b0c1c',
  },
  mineRow: {
    background: '#131530',
  },
  newRow: {
    background: '#1c1e36',
    transition: 'background 0.5s',
  },
  youTag: {
    fontSize: 9,
    fontWeight: 700,
    color: '#7b61ff',
    letterSpacing: '0.1em',
    background: '#7b61ff22',
    padding: '2px 4px',
    borderRadius: 3,
  },
  empty: {
    color: '#4a4d6e',
    textAlign: 'center' as const,
    padding: 18,
    fontSize: 12,
  },
};

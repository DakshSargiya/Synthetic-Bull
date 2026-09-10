import type { PortfolioState } from '../types';

interface Props {
  portfolio: PortfolioState;
  clientId: string;
}

const fmt = (n: number, digits = 2) =>
  n.toLocaleString('en-US', { minimumFractionDigits: digits, maximumFractionDigits: digits });

const pnlColor = (v: number) => (v > 0 ? '#00c853' : v < 0 ? '#ff3d71' : '#9294ad');
const pnlSign  = (v: number) => (v > 0 ? '+' : '');

export function PortfolioWidget({ portfolio: p, clientId }: Props) {
  const totalPnL = p.realizedPnL + p.unrealizedPnL;

  return (
    <div style={styles.container}>
      <div style={styles.header}>
        <span>Portfolio</span>
        <span style={styles.clientId}>{clientId.slice(0, 14)}</span>
      </div>

      {/* Total value — hero metric */}
      <div style={styles.heroRow}>
        <div style={styles.heroLabel}>Total Value</div>
        <div style={styles.heroValue}>${fmt(p.totalValue)}</div>
        <div style={{ fontSize: 12, color: pnlColor(totalPnL), fontFamily: 'JetBrains Mono, monospace', marginTop: 2 }}>
          {pnlSign(totalPnL)}${fmt(Math.abs(totalPnL))} all-time P&amp;L
        </div>
      </div>

      <div style={styles.divider} />

      {/* Grid of stats */}
      <div style={styles.grid}>
        <Stat label="Cash" value={`$${fmt(p.cash)}`} />
        <Stat
          label="Holdings"
          value={`${p.holdings >= 0 ? '+' : ''}${fmt(p.holdings, 4)}`}
          color={p.holdings > 0 ? '#00c853' : p.holdings < 0 ? '#ff3d71' : undefined}
          sub={p.holdings !== 0 ? `avg $${fmt(p.avgCost)}` : 'flat'}
        />
        <Stat
          label="Unrealised P&L"
          value={`${pnlSign(p.unrealizedPnL)}$${fmt(Math.abs(p.unrealizedPnL))}`}
          color={pnlColor(p.unrealizedPnL)}
        />
        <Stat
          label="Realised P&L"
          value={`${pnlSign(p.realizedPnL)}$${fmt(Math.abs(p.realizedPnL))}`}
          color={pnlColor(p.realizedPnL)}
        />
      </div>

      {/* Position bar */}
      {p.holdings !== 0 && (
        <div style={styles.posBar}>
          <span style={{ color: '#4a4d6e', fontSize: 11 }}>
            {p.holdings > 0 ? 'LONG' : 'SHORT'} position
          </span>
          <span style={{ color: p.holdings > 0 ? '#00c853' : '#ff3d71', fontSize: 11, fontFamily: 'JetBrains Mono, monospace' }}>
            {Math.abs(p.holdings).toFixed(4)} units @ ${fmt(p.avgCost)}
          </span>
        </div>
      )}
    </div>
  );
}

function Stat({ label, value, color, sub }: {
  label: string; value: string;
  color?: string; sub?: string;
}) {
  return (
    <div style={{ display: 'flex', flexDirection: 'column', gap: 2 }}>
      <div style={{ fontSize: 10, color: '#4a4d6e', textTransform: 'uppercase', letterSpacing: '0.08em' }}>
        {label}
      </div>
      <div style={{ fontSize: 13, fontFamily: 'JetBrains Mono, monospace', color: color ?? '#c2c4dc', fontWeight: 500 }}>
        {value}
      </div>
      {sub && <div style={{ fontSize: 10, color: '#4a4d6e' }}>{sub}</div>}
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
    display: 'flex',
    justifyContent: 'space-between',
    alignItems: 'center',
    padding: '12px 16px',
    borderBottom: '1px solid #1c1e36',
    fontWeight: 600,
    fontSize: 13,
    color: '#c2c4dc',
  },
  clientId: {
    fontSize: 10,
    color: '#4a4d6e',
    fontFamily: 'JetBrains Mono, monospace',
  },
  heroRow: {
    padding: '14px 16px 10px',
  },
  heroLabel: {
    fontSize: 11,
    color: '#4a4d6e',
    textTransform: 'uppercase' as const,
    letterSpacing: '0.08em',
    marginBottom: 2,
  },
  heroValue: {
    fontSize: 22,
    fontWeight: 700,
    fontFamily: 'JetBrains Mono, monospace',
    color: '#c2c4dc',
  },
  divider: {
    height: 1,
    background: '#1c1e36',
    margin: '0 16px',
  },
  grid: {
    display: 'grid',
    gridTemplateColumns: '1fr 1fr',
    gap: '14px 12px',
    padding: '14px 16px',
  },
  posBar: {
    display: 'flex',
    justifyContent: 'space-between',
    padding: '8px 16px',
    borderTop: '1px solid #1c1e36',
    background: '#12132a',
  },
};

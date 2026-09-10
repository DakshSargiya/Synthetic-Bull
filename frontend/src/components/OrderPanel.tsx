import { useState } from 'react';

interface Props {
  lastPrice: number;
  onLimit:  (side: 'BID' | 'ASK', price: number, qty: number) => void;
  onMarket: (side: 'BID' | 'ASK', qty: number) => void;
}

type OrderType = 'LIMIT' | 'MARKET';
type Side      = 'BID'   | 'ASK';

export function OrderPanel({ lastPrice, onLimit, onMarket }: Props) {
  const [orderType, setOrderType]   = useState<OrderType>('LIMIT');
  const [side, setSide]             = useState<Side>('BID');
  const [price, setPrice]           = useState('');
  const [qty, setQty]               = useState('');
  const [flash, setFlash]           = useState<string | null>(null);

  const priceVal = parseFloat(price);
  const qtyVal   = parseFloat(qty);
  const valid    = qtyVal > 0 && (orderType === 'MARKET' || priceVal > 0);

  function submit() {
    if (!valid) return;
    if (orderType === 'LIMIT') {
      onLimit(side, priceVal, qtyVal);
    } else {
      onMarket(side, qtyVal);
    }
    setFlash(side === 'BID' ? 'Bought!' : 'Sold!');
    setTimeout(() => setFlash(null), 1200);
    setQty('');
  }

  const accentBid = '#00c853';
  const accentAsk = '#ff3d71';
  const accent    = side === 'BID' ? accentBid : accentAsk;

  return (
    <div style={styles.container}>
      <div style={styles.header}>Place Order</div>

      {/* Order type tabs */}
      <div style={styles.tabs}>
        {(['LIMIT', 'MARKET'] as OrderType[]).map(t => (
          <button
            key={t}
            style={{ ...styles.tab, ...(orderType === t ? { ...styles.tabActive, borderBottomColor: accent } : {}) }}
            onClick={() => setOrderType(t)}
          >
            {t}
          </button>
        ))}
      </div>

      {/* Side toggle */}
      <div style={styles.sideRow}>
        <button
          style={{ ...styles.sideBtn, ...(side === 'BID' ? { background: accentBid + '22', color: accentBid, borderColor: accentBid } : {}) }}
          onClick={() => setSide('BID')}
        >
          BUY / BID
        </button>
        <button
          style={{ ...styles.sideBtn, ...(side === 'ASK' ? { background: accentAsk + '22', color: accentAsk, borderColor: accentAsk } : {}) }}
          onClick={() => setSide('ASK')}
        >
          SELL / ASK
        </button>
      </div>

      {/* Price input (limit only) */}
      {orderType === 'LIMIT' && (
        <div style={styles.fieldGroup}>
          <label style={styles.label}>Price (USD)</label>
          <div style={styles.inputWrap}>
            <input
              style={styles.input}
              type="number"
              min="0"
              step="0.01"
              placeholder={lastPrice.toFixed(2)}
              value={price}
              onChange={e => setPrice(e.target.value)}
            />
            <span style={styles.inputUnit}>$</span>
          </div>
          {/* Quick-fill buttons */}
          <div style={styles.quickRow}>
            {[-0.5, -0.1, +0.1, +0.5].map(pct => (
              <button
                key={pct}
                style={styles.quickBtn}
                onClick={() => setPrice((lastPrice * (1 + pct / 100)).toFixed(2))}
              >
                {pct > 0 ? '+' : ''}{pct}%
              </button>
            ))}
          </div>
        </div>
      )}

      {/* Quantity input */}
      <div style={styles.fieldGroup}>
        <label style={styles.label}>Quantity (units)</label>
        <div style={styles.inputWrap}>
          <input
            style={styles.input}
            type="number"
            min="0"
            step="0.01"
            placeholder="0.00"
            value={qty}
            onChange={e => setQty(e.target.value)}
          />
          <span style={styles.inputUnit}>QTY</span>
        </div>
        {/* Preset qty shortcuts */}
        <div style={styles.quickRow}>
          {[0.01, 0.1, 0.5, 1.0].map(q => (
            <button key={q} style={styles.quickBtn} onClick={() => setQty(String(q))}>
              {q}
            </button>
          ))}
        </div>
      </div>

      {/* Notional value estimate */}
      {qtyVal > 0 && (
        <div style={styles.notional}>
          ≈ ${((orderType === 'LIMIT' ? priceVal || lastPrice : lastPrice) * qtyVal).toLocaleString('en-US', { maximumFractionDigits: 2 })} notional
        </div>
      )}

      {/* Submit button */}
      <button
        disabled={!valid}
        style={{
          ...styles.submitBtn,
          background: valid ? accent : '#1c1e36',
          color: valid ? '#fff' : '#4a4d6e',
          cursor: valid ? 'pointer' : 'not-allowed',
        }}
        onClick={submit}
      >
        {flash ?? `${orderType === 'LIMIT' ? 'Place Limit' : 'Execute Market'} ${side === 'BID' ? 'Buy' : 'Sell'}`}
      </button>
    </div>
  );
}

const styles: Record<string, React.CSSProperties> = {
  container: {
    background: '#0d0e21',
    borderRadius: 10,
    border: '1px solid #1c1e36',
    padding: '0 0 16px',
    fontFamily: 'Inter, sans-serif',
  },
  header: {
    padding: '12px 16px 0',
    fontWeight: 600,
    fontSize: 13,
    color: '#c2c4dc',
  },
  tabs: {
    display: 'flex',
    borderBottom: '1px solid #1c1e36',
    margin: '10px 0 0',
  },
  tab: {
    flex: 1,
    padding: '8px 0',
    background: 'transparent',
    border: 'none',
    borderBottom: '2px solid transparent',
    color: '#4a4d6e',
    fontSize: 12,
    fontFamily: 'Inter, sans-serif',
    fontWeight: 600,
    cursor: 'pointer',
    letterSpacing: '0.05em',
  },
  tabActive: {
    color: '#c2c4dc',
    borderBottomWidth: 2,
    borderBottomStyle: 'solid',
  },
  sideRow: {
    display: 'flex',
    gap: 8,
    padding: '12px 16px 0',
  },
  sideBtn: {
    flex: 1,
    padding: '8px 0',
    borderRadius: 6,
    border: '1px solid #1c1e36',
    background: 'transparent',
    color: '#4a4d6e',
    fontSize: 12,
    fontWeight: 600,
    cursor: 'pointer',
    letterSpacing: '0.06em',
    fontFamily: 'Inter, sans-serif',
    transition: 'all 0.15s',
  },
  fieldGroup: {
    padding: '12px 16px 0',
    display: 'flex',
    flexDirection: 'column',
    gap: 4,
  },
  label: {
    fontSize: 11,
    color: '#4a4d6e',
    textTransform: 'uppercase' as const,
    letterSpacing: '0.08em',
  },
  inputWrap: {
    position: 'relative',
  },
  input: {
    width: '100%',
    padding: '8px 40px 8px 10px',
    background: '#12132a',
    border: '1px solid #1c1e36',
    borderRadius: 6,
    color: '#c2c4dc',
    fontSize: 14,
    fontFamily: 'JetBrains Mono, monospace',
    outline: 'none',
    boxSizing: 'border-box' as const,
  },
  inputUnit: {
    position: 'absolute',
    right: 10,
    top: '50%',
    transform: 'translateY(-50%)',
    fontSize: 11,
    color: '#4a4d6e',
  },
  quickRow: {
    display: 'flex',
    gap: 4,
    marginTop: 4,
  },
  quickBtn: {
    flex: 1,
    padding: '4px 0',
    background: '#12132a',
    border: '1px solid #1c1e36',
    borderRadius: 4,
    color: '#9294ad',
    fontSize: 11,
    cursor: 'pointer',
    fontFamily: 'Inter, sans-serif',
  },
  notional: {
    margin: '10px 16px 0',
    fontSize: 12,
    color: '#4a4d6e',
    fontFamily: 'JetBrains Mono, monospace',
  },
  submitBtn: {
    display: 'block',
    width: 'calc(100% - 32px)',
    margin: '16px 16px 0',
    padding: '12px 0',
    borderRadius: 8,
    border: 'none',
    fontSize: 13,
    fontWeight: 700,
    fontFamily: 'Inter, sans-serif',
    letterSpacing: '0.05em',
    transition: 'all 0.2s',
  },
};

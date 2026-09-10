import type { OpenOrder } from '../types';

interface Props {
  orders: OpenOrder[];
  onCancel: (orderId: number) => void;
}

export function OpenOrders({ orders, onCancel }: Props) {
  return (
    <div style={styles.container}>
      <div style={styles.header}>
        Open Orders {orders.length > 0 && <span style={styles.count}>{orders.length}</span>}
      </div>

      {orders.length === 0 ? (
        <div style={styles.empty}>No resting orders</div>
      ) : (
        <div style={styles.list}>
          {orders.map(o => (
            <div key={o.orderId} style={styles.row}>
              <span style={{ ...styles.side, color: o.side === 'BID' ? '#00c853' : '#ff3d71' }}>
                {o.side === 'BID' ? 'BUY' : 'SELL'}
              </span>
              <span style={styles.price}>${o.price.toFixed(2)}</span>
              <span style={styles.qty}>{o.remainingQty.toFixed(4)}</span>
              <button
                style={styles.cancelBtn}
                onClick={() => onCancel(o.orderId)}
                title="Cancel order"
              >
                ✕
              </button>
            </div>
          ))}
        </div>
      )}
    </div>
  );
}

const styles: Record<string, React.CSSProperties> = {
  container: {
    background: '#0d0e21',
    borderRadius: 10,
    border: '1px solid #1c1e36',
    fontFamily: 'Inter, sans-serif',
  },
  header: {
    padding: '12px 16px',
    fontWeight: 600,
    fontSize: 13,
    color: '#c2c4dc',
    display: 'flex',
    alignItems: 'center',
    gap: 6,
    borderBottom: '1px solid #1c1e36',
  },
  count: {
    fontSize: 11,
    color: '#7b61ff',
    background: '#7b61ff22',
    borderRadius: 10,
    padding: '1px 7px',
  },
  empty: {
    padding: '16px',
    fontSize: 12,
    color: '#4a4d6e',
  },
  list: {
    maxHeight: 160,
    overflowY: 'auto',
  },
  row: {
    display: 'grid',
    gridTemplateColumns: '40px 1fr 1fr 24px',
    alignItems: 'center',
    gap: 6,
    padding: '6px 16px',
    fontSize: 12,
    fontFamily: 'JetBrains Mono, monospace',
    borderBottom: '1px solid #1c1e3688',
  },
  side: {
    fontWeight: 700,
    fontSize: 10,
  },
  price: {
    color: '#c2c4dc',
  },
  qty: {
    color: '#9294ad',
    textAlign: 'right' as const,
  },
  cancelBtn: {
    background: 'transparent',
    border: '1px solid #1c1e36',
    borderRadius: 4,
    color: '#4a4d6e',
    fontSize: 11,
    cursor: 'pointer',
    padding: '2px 0',
    lineHeight: 1,
  },
};

import { useEffect, useRef } from 'react';
import {
  createChart, IChartApi, ISeriesApi,
  CandlestickData, HistogramData,
  CrosshairMode, LineStyle,
} from 'lightweight-charts';
import type { CandleData } from '../types';

interface Props {
  candles: CandleData[];
  lastPrice: number;
}

export function CandlestickChart({ candles, lastPrice }: Props) {
  const containerRef = useRef<HTMLDivElement>(null);
  const chartRef     = useRef<IChartApi | null>(null);
  const candleRef    = useRef<ISeriesApi<'Candlestick'> | null>(null);
  const volRef       = useRef<ISeriesApi<'Histogram'> | null>(null);

  // ---- Create chart once ----
  useEffect(() => {
    if (!containerRef.current) return;

    const chart = createChart(containerRef.current, {
      layout: {
        background: { color: '#0d0e21' },
        textColor:  '#9294ad',
        fontFamily: 'Inter, sans-serif',
      },
      grid: {
        vertLines: { color: '#161829', style: LineStyle.Solid },
        horzLines: { color: '#161829', style: LineStyle.Solid },
      },
      crosshair: {
        mode: CrosshairMode.Normal,
        vertLine: { color: '#4a4d6e', style: LineStyle.Dashed },
        horzLine: { color: '#4a4d6e', style: LineStyle.Dashed },
      },
      rightPriceScale: {
        borderColor: '#1c1e36',
        scaleMargins: { top: 0.05, bottom: 0.25 },
      },
      timeScale: {
        borderColor: '#1c1e36',
        timeVisible: true,
        secondsVisible: true,
      },
      handleScale: true,
      handleScroll: true,
    });

    // Candlestick series
    const cSeries = chart.addCandlestickSeries({
      upColor:          '#00c853',
      downColor:        '#ff3d71',
      borderUpColor:    '#00c853',
      borderDownColor:  '#ff3d71',
      wickUpColor:      '#00c853',
      wickDownColor:    '#ff3d71',
      priceLineColor:   '#7b61ff',
      priceLineWidth:   1,
      lastValueVisible: true,
    });

    // Volume histogram (lower pane)
    const vSeries = chart.addHistogramSeries({
      color:        '#2d2f4e',
      priceFormat:  { type: 'volume' },
      priceScaleId: 'vol',
    });
    chart.priceScale('vol').applyOptions({
      scaleMargins: { top: 0.8, bottom: 0 },
    });

    chartRef.current  = chart;
    candleRef.current = cSeries;
    volRef.current    = vSeries;

    const observer = new ResizeObserver(() => {
      chart.applyOptions({
        width:  containerRef.current?.clientWidth  ?? 600,
        height: containerRef.current?.clientHeight ?? 400,
      });
    });
    observer.observe(containerRef.current);

    return () => {
      observer.disconnect();
      chart.remove();
    };
  }, []);

  // ---- Feed data ----
  useEffect(() => {
    if (!candleRef.current || !volRef.current || candles.length === 0) return;

    const cd: CandlestickData[] = candles.map(c => ({
      time: c.time as any,
      open: c.open, high: c.high, low: c.low, close: c.close,
    }));
    const vd: HistogramData[] = candles.map(c => ({
      time: c.time as any,
      value: c.volume,
      color: c.close >= c.open ? '#1a4d34' : '#4d1a27',
    }));

    candleRef.current.setData(cd);
    volRef.current.setData(vd);
  }, [candles]);

  return (
    <div
      ref={containerRef}
      style={{ width: '100%', height: '100%', minHeight: 340 }}
    />
  );
}

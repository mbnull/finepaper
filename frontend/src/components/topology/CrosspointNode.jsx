export default function CrosspointNode({ x, y }) {
  const cx = x * 100
  const cy = y * 100
  const coreSize = 16
  const stubLength = 12

  return (
    <g>
      {/* Central core */}
      <rect
        x={cx - coreSize/2}
        y={cy - coreSize/2}
        width={coreSize}
        height={coreSize}
        fill="#4a90e2"
        stroke="#2c5aa0"
        strokeWidth="2"
      />

      {/* Port stubs - N, S, E, W */}
      <line x1={cx} y1={cy - coreSize/2} x2={cx} y2={cy - coreSize/2 - stubLength} stroke="#2c5aa0" strokeWidth="3" />
      <line x1={cx} y1={cy + coreSize/2} x2={cx} y2={cy + coreSize/2 + stubLength} stroke="#2c5aa0" strokeWidth="3" />
      <line x1={cx + coreSize/2} y1={cy} x2={cx + coreSize/2 + stubLength} y2={cy} stroke="#2c5aa0" strokeWidth="3" />
      <line x1={cx - coreSize/2} y1={cy} x2={cx - coreSize/2 - stubLength} y2={cy} stroke="#2c5aa0" strokeWidth="3" />

      {/* Local port indicator */}
      <circle cx={cx} cy={cy} r={3} fill="#ffd700" stroke="#333" strokeWidth="1" />
    </g>
  )
}

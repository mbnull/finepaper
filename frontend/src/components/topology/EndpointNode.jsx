export default function EndpointNode({ endpoint, crosspoints, index = 0 }) {
  const cp = crosspoints.find(c => c.id === endpoint.attached_to)
  if (!cp) return null

  const color = endpoint.type === 'master' ? '#e74c3c' : '#2ecc71'
  const offset = 40
  const positions = [
    { dx: 0, dy: -offset },
    { dx: offset, dy: 0 },
    { dx: 0, dy: offset },
    { dx: -offset, dy: 0 }
  ]
  const pos = positions[index % 4]
  const ex = cp.x * 100 + pos.dx
  const ey = cp.y * 100 + pos.dy
  const stubLength = 8

  return (
    <g>
      {/* Endpoint box */}
      <rect
        x={ex - 12}
        y={ey - 12}
        width={24}
        height={24}
        fill={color}
        stroke="#333"
        strokeWidth="2"
        rx={2}
      />
      {/* Label */}
      <text
        x={ex}
        y={ey + 4}
        fontSize="10"
        fontWeight="bold"
        textAnchor="middle"
        fill="#fff"
      >
        {endpoint.id}
      </text>
      {/* Port stub extending toward crosspoint */}
      <line
        x1={ex - pos.dx * 0.3}
        y1={ey - pos.dy * 0.3}
        x2={ex - pos.dx * 0.5}
        y2={ey - pos.dy * 0.5}
        stroke="#333"
        strokeWidth="3"
      />
      {/* Connection line to crosspoint local port */}
      <line x1={ex - pos.dx * 0.5} y1={ey - pos.dy * 0.5} x2={cp.x * 100} y2={cp.y * 100} stroke="#666" strokeWidth="2" />
    </g>
  )
}

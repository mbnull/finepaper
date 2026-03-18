export default function ConnectionLine({ from, to, crosspoints }) {
  const fromNode = crosspoints.find(cp => cp.id === from)
  const toNode = crosspoints.find(cp => cp.id === to)

  if (!fromNode || !toNode) return null

  return (
    <line
      x1={fromNode.x * 100}
      y1={fromNode.y * 100}
      x2={toNode.x * 100}
      y2={toNode.y * 100}
      stroke="#666"
      strokeWidth={2}
      markerEnd="url(#arrow)"
    />
  )
}

import { useState } from 'react'
import { generateMesh } from '../utils/meshGenerator'
import CrosspointNode from './topology/CrosspointNode'
import ConnectionLine from './topology/ConnectionLine'
import EndpointNode from './topology/EndpointNode'

export default function TopologyCanvas({ meshDimensions, endpoints = [] }) {
  const { crosspoints, connections } = generateMesh(meshDimensions)
  const [zoom, setZoom] = useState(1)
  const [pan, setPan] = useState({ x: 25, y: 25 })
  const [dragging, setDragging] = useState(false)
  const [dragStart, setDragStart] = useState({ x: 0, y: 0 })

  const width = meshDimensions.width * 100 + 50
  const height = meshDimensions.height * 100 + 50

  const handleWheel = (e) => {
    e.preventDefault()
    setZoom(z => Math.max(0.5, Math.min(3, z + (e.deltaY > 0 ? -0.1 : 0.1))))
  }

  const handleMouseDown = (e) => {
    setDragging(true)
    setDragStart({ x: e.clientX - pan.x, y: e.clientY - pan.y })
  }

  const handleMouseMove = (e) => {
    if (dragging) {
      setPan({ x: e.clientX - dragStart.x, y: e.clientY - dragStart.y })
    }
  }

  const handleMouseUp = () => setDragging(false)

  return (
    <svg
      width={width}
      height={height}
      style={{ border: '1px solid #ccc', cursor: dragging ? 'grabbing' : 'grab' }}
      onWheel={handleWheel}
      onMouseDown={handleMouseDown}
      onMouseMove={handleMouseMove}
      onMouseUp={handleMouseUp}
      onMouseLeave={handleMouseUp}
    >
      <defs>
        <marker id="arrow" markerWidth="10" markerHeight="10" refX="5" refY="3" orient="auto" markerUnits="strokeWidth">
          <path d="M0,0 L0,6 L9,3 z" fill="#666" />
        </marker>
      </defs>
      <g transform={`translate(${pan.x}, ${pan.y}) scale(${zoom})`}>
        {connections.map((conn, i) => (
          <ConnectionLine
            key={i}
            from={conn.from}
            to={conn.to}
            crosspoints={crosspoints}
          />
        ))}
        {crosspoints.map(cp => (
          <CrosspointNode key={cp.id} x={cp.x} y={cp.y} />
        ))}
        {endpoints.map((ep, i) => {
          const sameXpEndpoints = endpoints.filter(e => e.attached_to === ep.attached_to)
          const index = sameXpEndpoints.indexOf(ep)
          return <EndpointNode key={ep.id} endpoint={ep} crosspoints={crosspoints} index={index} />
        })}
      </g>
    </svg>
  )
}

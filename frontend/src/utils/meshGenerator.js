export function generateCrosspoints(width, height) {
  const crosspoints = []
  for (let y = 0; y < height; y++) {
    for (let x = 0; x < width; x++) {
      crosspoints.push({ id: `xp_${x}_${y}`, x, y })
    }
  }
  return crosspoints
}

export function generateMesh({ width, height, routing_algorithm }) {
  const crosspoints = []
  const connections = []

  for (let y = 0; y < height; y++) {
    for (let x = 0; x < width; x++) {
      crosspoints.push({ id: `xp_${x}_${y}`, x, y, routing_algorithm })

      if (x < width - 1) {
        connections.push({ from: `xp_${x}_${y}`, to: `xp_${x + 1}_${y}`, dir: 'east' })
      }
      if (y < height - 1) {
        connections.push({ from: `xp_${x}_${y}`, to: `xp_${x}_${y + 1}`, dir: 'south' })
      }
    }
  }

  return { crosspoints, connections }
}

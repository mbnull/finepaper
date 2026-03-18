export function exportToJson(config, endpoints) {
  const xps = []
  for (let y = 0; y < config.meshDimensions.height; y++) {
    for (let x = 0; x < config.meshDimensions.width; x++) {
      const xpId = `xp_${x}_${y}`
      const xpEndpoints = endpoints.filter(ep => ep.attached_to === xpId).map(ep => ep.id)
      xps.push({
        id: xpId,
        x,
        y,
        endpoints: xpEndpoints,
        config: {
          routing_algorithm: config.routingAlgorithm,
          vc_count: config.vcCount || 2,
          buffer_depth: config.bufferDepth || 4
        }
      })
    }
  }

  const connections = []
  for (let y = 0; y < config.meshDimensions.height; y++) {
    for (let x = 0; x < config.meshDimensions.width; x++) {
      if (x < config.meshDimensions.width - 1) {
        connections.push({
          from: `xp_${x}_${y}`,
          to: `xp_${x + 1}_${y}`,
          dir: "east"
        })
      }
      if (y < config.meshDimensions.height - 1) {
        connections.push({
          from: `xp_${x}_${y}`,
          to: `xp_${x}_${y + 1}`,
          dir: "south"
        })
      }
    }
  }

  const nocConfig = {
    name: config.name,
    version: "1.0",
    xps,
    connections,
    endpoints: endpoints.map(ep => ({
      id: ep.id,
      type: ep.type,
      protocol: ep.protocol || "axi4",
      data_width: ep.data_width || 64
    }))
  }

  const blob = new Blob([JSON.stringify(nocConfig, null, 2)], { type: 'application/json' })
  const url = URL.createObjectURL(blob)
  const a = document.createElement('a')
  a.href = url
  a.download = `${config.name}.json`
  a.click()
  URL.revokeObjectURL(url)
}

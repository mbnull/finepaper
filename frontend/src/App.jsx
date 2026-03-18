import { useState } from 'react'
import Wizard from './components/Wizard'
import TopologyCanvas from './components/TopologyCanvas'
import ExportButton from './components/ExportButton'
import EndpointPanel from './components/wizard/EndpointPanel'
import { generateMesh, generateCrosspoints } from './utils/meshGenerator'

function App() {
  const [config, setConfig] = useState(null)
  const [endpoints, setEndpoints] = useState([])

  const handleWizardComplete = (wizardConfig) => {
    setConfig(wizardConfig)
    setEndpoints(wizardConfig.endpoints || [])
  }

  if (!config) {
    return (
      <div className="min-h-screen bg-gradient-to-br from-blue-50 to-indigo-100">
        <div className="container mx-auto px-4 py-8">
          <div className="text-center mb-8">
            <h1 className="text-4xl font-bold text-gray-800 mb-2">NoC Configuration Tool</h1>
            <p className="text-gray-600">Network-on-Chip IP Core Configuration</p>
          </div>
          <Wizard onComplete={handleWizardComplete} />
        </div>
      </div>
    )
  }

  const mesh = generateMesh({ ...config.meshDimensions, routing_algorithm: config.routingAlgorithm })
  const fullConfig = { ...config, mesh }

  const crosspoints = generateCrosspoints(config.meshDimensions.width, config.meshDimensions.height)

  const handleAddEndpoint = (ep) => setEndpoints([...endpoints, ep])
  const handleRemoveEndpoint = (id) => setEndpoints(endpoints.filter(e => e.id !== id))

  return (
    <div className="min-h-screen bg-gradient-to-br from-blue-50 to-indigo-100">
      <div className="container mx-auto px-4 py-8">
        <div className="text-center mb-6">
          <h1 className="text-4xl font-bold text-gray-800 mb-2">NoC Configuration Tool</h1>
          <p className="text-gray-600">{config.name} - {config.meshDimensions.width}×{config.meshDimensions.height} Mesh</p>
        </div>
        <div className="mb-4 flex justify-center">
          <ExportButton config={fullConfig} endpoints={endpoints} />
        </div>
        <div className="flex gap-6">
          <div className="flex-1 bg-white rounded-lg shadow-lg p-6">
            <h2 className="text-xl font-semibold text-gray-800 mb-4">Topology Visualization</h2>
            <TopologyCanvas meshDimensions={config.meshDimensions} endpoints={endpoints} />
          </div>
          <div className="w-96 bg-white rounded-lg shadow-lg p-6">
            <EndpointPanel
              endpoints={endpoints}
              crosspoints={crosspoints}
              onAdd={handleAddEndpoint}
              onRemove={handleRemoveEndpoint}
            />
          </div>
        </div>
      </div>
    </div>
  )
}

export default App

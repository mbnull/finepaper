export default function NocParamsPanel({ vcCount, bufferDepth, onChange }) {
  return (
    <div>
      <h2 className="text-2xl font-bold text-gray-800 mb-2">NoC Parameters</h2>
      <p className="text-gray-600 mb-6">Configure virtual channels and buffer depth</p>
      <div className="space-y-6">
        <div>
          <label className="block text-sm font-semibold text-gray-700 mb-2">Virtual Channels</label>
          <input
            type="number"
            value={vcCount}
            onChange={e => onChange('vcCount', Number(e.target.value))}
            min="1"
            max="8"
            className="w-full p-3 border-2 border-gray-300 rounded-lg focus:border-blue-500 focus:outline-none transition-colors text-lg"
          />
        </div>
        <div>
          <label className="block text-sm font-semibold text-gray-700 mb-2">Buffer Depth</label>
          <input
            type="number"
            value={bufferDepth}
            onChange={e => onChange('bufferDepth', Number(e.target.value))}
            min="1"
            max="16"
            className="w-full p-3 border-2 border-gray-300 rounded-lg focus:border-blue-500 focus:outline-none transition-colors text-lg"
          />
        </div>
      </div>
    </div>
  )
}

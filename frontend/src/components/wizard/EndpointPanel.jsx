import { useState } from 'react'

export default function EndpointPanel({ endpoints, crosspoints, onAdd, onRemove }) {
  const [id, setId] = useState('')
  const [type, setType] = useState('master')
  const [protocol, setProtocol] = useState('axi4')
  const [dataWidth, setDataWidth] = useState(64)
  const [attachedTo, setAttachedTo] = useState('')

  const handleAdd = () => {
    if (!id || !attachedTo) return
    if (endpoints.some(ep => ep.id === id)) return
    onAdd({ id, type, protocol, data_width: dataWidth, attached_to: attachedTo })
    setId('')
  }

  return (
    <div>
      <h2 className="text-2xl font-bold text-gray-800 mb-2">Configure Endpoints</h2>
      <p className="text-gray-600 mb-6">Add endpoints to your NoC topology</p>
      <div className="space-y-3 mb-6 p-4 bg-gradient-to-br from-gray-50 to-gray-100 rounded-lg border border-gray-200">
        <input value={id} onChange={e => setId(e.target.value)} placeholder="Endpoint ID" className="w-full p-3 border-2 border-gray-300 rounded-lg focus:border-blue-500 focus:outline-none transition-colors" />
        <div className="grid grid-cols-2 gap-3">
          <select value={type} onChange={e => setType(e.target.value)} className="p-3 border-2 border-gray-300 rounded-lg focus:border-blue-500 focus:outline-none transition-colors bg-white">
            <option value="master">Master</option>
            <option value="slave">Slave</option>
          </select>
          <select value={protocol} onChange={e => setProtocol(e.target.value)} className="p-3 border-2 border-gray-300 rounded-lg focus:border-blue-500 focus:outline-none transition-colors bg-white">
            <option value="axi4">AXI4</option>
          </select>
        </div>
        <div className="grid grid-cols-2 gap-3">
          <select value={dataWidth} onChange={e => setDataWidth(Number(e.target.value))} className="p-3 border-2 border-gray-300 rounded-lg focus:border-blue-500 focus:outline-none transition-colors bg-white">
            <option value={32}>32-bit</option>
            <option value={64}>64-bit</option>
            <option value={128}>128-bit</option>
            <option value={256}>256-bit</option>
          </select>
          <select value={attachedTo} onChange={e => setAttachedTo(e.target.value)} className="p-3 border-2 border-gray-300 rounded-lg focus:border-blue-500 focus:outline-none transition-colors bg-white">
            <option value="">Select XP</option>
            {crosspoints.map(cp => <option key={cp.id} value={cp.id}>{cp.id}</option>)}
          </select>
        </div>
        <button onClick={handleAdd} className="w-full bg-gradient-to-r from-blue-600 to-indigo-600 text-white p-3 rounded-lg hover:from-blue-700 hover:to-indigo-700 transition-all font-semibold shadow-md">Add Endpoint</button>
      </div>
      {endpoints.length > 0 && (
        <div>
          <h3 className="font-semibold text-gray-800 mb-3">Configured Endpoints ({endpoints.length})</h3>
          <ul className="space-y-2 max-h-48 overflow-y-auto">
            {endpoints.map(ep => (
              <li key={ep.id} className="flex justify-between items-center p-3 bg-white border-2 border-gray-200 rounded-lg hover:border-blue-300 transition-colors">
                <span className="font-medium text-gray-700">{ep.id} <span className="text-gray-500 text-sm">@ {ep.attached_to}</span></span>
                <button onClick={() => onRemove(ep.id)} className="bg-red-500 text-white px-4 py-1.5 rounded-lg text-sm hover:bg-red-600 transition-colors font-medium">Remove</button>
              </li>
            ))}
          </ul>
        </div>
      )}
    </div>
  )
}

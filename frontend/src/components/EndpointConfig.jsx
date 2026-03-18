import { useState } from 'react'

export default function EndpointConfig({ crosspoints, endpoints, onAddEndpoint }) {
  const [formData, setFormData] = useState({
    id: '',
    type: 'master',
    protocol: 'axi4',
    data_width: 64,
    attached_to: ''
  })

  const handleSubmit = (e) => {
    e.preventDefault()
    if (!formData.id || !formData.attached_to) return
    onAddEndpoint(formData)
    setFormData({ ...formData, id: '' })
  }

  return (
    <div style={{ padding: '20px', borderTop: '1px solid #ccc' }}>
      <h3>Add Endpoint</h3>
      <form onSubmit={handleSubmit} style={{ display: 'flex', gap: '10px', alignItems: 'end' }}>
        <label>
          ID:
          <input
            type="text"
            value={formData.id}
            onChange={e => setFormData({ ...formData, id: e.target.value })}
            placeholder="ep_cpu0"
            required
          />
        </label>

        <label>
          Type:
          <select value={formData.type} onChange={e => setFormData({ ...formData, type: e.target.value })}>
            <option value="master">master</option>
            <option value="slave">slave</option>
          </select>
        </label>

        <label>
          Protocol:
          <select value={formData.protocol} onChange={e => setFormData({ ...formData, protocol: e.target.value })}>
            <option value="axi4">axi4</option>
          </select>
        </label>

        <label>
          Data Width:
          <select value={formData.data_width} onChange={e => setFormData({ ...formData, data_width: Number(e.target.value) })}>
            <option value={32}>32</option>
            <option value={64}>64</option>
            <option value={128}>128</option>
            <option value={256}>256</option>
          </select>
        </label>

        <label>
          Attach to:
          <select value={formData.attached_to} onChange={e => setFormData({ ...formData, attached_to: e.target.value })} required>
            <option value="">Select crosspoint</option>
            {crosspoints.map(cp => (
              <option key={cp.id} value={cp.id}>{cp.id}</option>
            ))}
          </select>
        </label>

        <button type="submit">Add Endpoint</button>
      </form>

      {endpoints.length > 0 && (
        <div style={{ marginTop: '20px' }}>
          <h4>Endpoints ({endpoints.length})</h4>
          <ul>
            {endpoints.map(ep => (
              <li key={ep.id}>
                {ep.id} ({ep.type}) - {ep.data_width}bit {ep.protocol} @ {ep.attached_to}
              </li>
            ))}
          </ul>
        </div>
      )}
    </div>
  )
}

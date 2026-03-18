import { exportToJson } from '../utils/exportJson'

export default function ExportButton({ config, endpoints }) {
  const handleExport = () => {
    if (!config.mesh.crosspoints.length) {
      alert('No configuration to export')
      return
    }
    exportToJson(config, endpoints)
  }

  return (
    <button
      onClick={handleExport}
      className="px-8 py-3 bg-gradient-to-r from-green-600 to-emerald-600 text-white rounded-lg hover:from-green-700 hover:to-emerald-700 transition-all font-semibold shadow-lg hover:shadow-xl transform hover:scale-105"
    >
      📥 Export JSON
    </button>
  )
}

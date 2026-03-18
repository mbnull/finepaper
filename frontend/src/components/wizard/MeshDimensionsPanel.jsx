export default function MeshDimensionsPanel({ width, height, onChange }) {
  return (
    <div>
      <h2 className="text-2xl font-bold text-gray-800 mb-2">Mesh Dimensions</h2>
      <p className="text-gray-600 mb-6">Define the size of your mesh topology</p>
      <div className="grid grid-cols-2 gap-6">
        <div>
          <label className="block text-sm font-semibold text-gray-700 mb-2">Width</label>
          <input
            type="number"
            value={width}
            onChange={(e) => onChange('width', parseInt(e.target.value) || 0)}
            min="1"
            className="w-full p-3 border-2 border-gray-300 rounded-lg focus:border-blue-500 focus:outline-none transition-colors text-lg"
          />
        </div>
        <div>
          <label className="block text-sm font-semibold text-gray-700 mb-2">Height</label>
          <input
            type="number"
            value={height}
            onChange={(e) => onChange('height', parseInt(e.target.value) || 0)}
            min="1"
            className="w-full p-3 border-2 border-gray-300 rounded-lg focus:border-blue-500 focus:outline-none transition-colors text-lg"
          />
        </div>
      </div>
      <div className="mt-4 p-4 bg-blue-50 rounded-lg border border-blue-200">
        <p className="text-sm text-gray-700">
          <span className="font-semibold">Total crosspoints:</span> {width * height}
        </p>
      </div>
    </div>
  );
}

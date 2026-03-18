export default function RoutingPanel({ value, onChange }) {
  const algorithms = ['xy', 'yx', 'west_first', 'north_last', 'negative_first'];

  return (
    <div>
      <h2 className="text-2xl font-bold text-gray-800 mb-2">Routing Algorithm</h2>
      <p className="text-gray-600 mb-6">Select the routing algorithm for your NoC</p>
      <select
        value={value}
        onChange={(e) => onChange(e.target.value)}
        className="w-full p-3 border-2 border-gray-300 rounded-lg focus:border-blue-500 focus:outline-none transition-colors text-lg bg-white"
      >
        {algorithms.map(alg => (
          <option key={alg} value={alg}>{alg}</option>
        ))}
      </select>
    </div>
  );
}

export default function NamePanel({ value, onChange }) {
  return (
    <div>
      <h2 className="text-2xl font-bold text-gray-800 mb-2">Configuration Name</h2>
      <p className="text-gray-600 mb-6">Give your NoC configuration a descriptive name</p>
      <input
        type="text"
        value={value}
        onChange={(e) => onChange(e.target.value)}
        placeholder="e.g., my_noc_config"
        className="w-full p-3 border-2 border-gray-300 rounded-lg focus:border-blue-500 focus:outline-none transition-colors text-lg"
      />
    </div>
  );
}

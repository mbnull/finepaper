export default function ComponentTypePanel({ value, onChange }) {
  return (
    <div>
      <h2 className="text-2xl font-bold text-gray-800 mb-2">Select Component Type</h2>
      <p className="text-gray-600 mb-6">
        Choose the type of interconnect component to configure.
      </p>
      <label className="flex items-start p-4 border-2 border-blue-200 rounded-lg cursor-pointer hover:bg-blue-50 hover:border-blue-400 transition-all">
        <input
          type="radio"
          name="componentType"
          value="noc"
          checked={value === 'noc'}
          onChange={(e) => onChange(e.target.value)}
          className="mt-1 mr-4 w-5 h-5 text-blue-600"
        />
        <div>
          <div className="font-semibold text-lg text-gray-800">Network-on-Chip (NoC)</div>
          <div className="text-sm text-gray-600 mt-1">
            A packet-switched network for on-chip communication between processing elements
          </div>
        </div>
      </label>
    </div>
  );
}

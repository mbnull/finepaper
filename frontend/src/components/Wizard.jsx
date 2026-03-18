import { useState } from 'react';
import ComponentTypePanel from './wizard/ComponentTypePanel';
import NamePanel from './wizard/NamePanel';
import MeshDimensionsPanel from './wizard/MeshDimensionsPanel';
import RoutingPanel from './wizard/RoutingPanel';
import NocParamsPanel from './wizard/NocParamsPanel';
import EndpointPanel from './wizard/EndpointPanel';
import { generateCrosspoints } from '../utils/meshGenerator';

export default function Wizard({ onComplete }) {
  const [page, setPage] = useState(0);
  const [componentType, setComponentType] = useState('');
  const [name, setName] = useState('');
  const [width, setWidth] = useState(3);
  const [height, setHeight] = useState(3);
  const [routingAlgorithm, setRoutingAlgorithm] = useState('xy');
  const [vcCount, setVcCount] = useState(2);
  const [bufferDepth, setBufferDepth] = useState(4);
  const [endpoints, setEndpoints] = useState([]);

  const isPageValid = () => {
    if (page === 0) return componentType.length > 0;
    if (page === 1) return name.trim().length > 0;
    if (page === 2) return width > 0 && height > 0;
    return true;
  };

  const crosspoints = generateCrosspoints(width, height);

  const handleAddEndpoint = (ep) => setEndpoints([...endpoints, ep]);
  const handleRemoveEndpoint = (id) => setEndpoints(endpoints.filter(e => e.id !== id));

  const handleNext = () => {
    if (page === 5) {
      const validXpIds = new Set(crosspoints.map(cp => cp.id));
      const validEndpoints = endpoints.filter(ep => validXpIds.has(ep.attached_to));
      onComplete({
        componentType,
        name,
        version: '1.0',
        meshDimensions: { width, height },
        routingAlgorithm,
        vcCount,
        bufferDepth,
        xps: [],
        connections: [],
        endpoints: validEndpoints
      });
    } else {
      setPage(page + 1);
    }
  };

  const handleCancel = () => {
    setPage(0);
    setComponentType('');
    setName('');
    setWidth(3);
    setHeight(3);
    setRoutingAlgorithm('xy');
    setVcCount(2);
    setBufferDepth(4);
    setEndpoints([]);
  };

  return (
    <div className="max-w-2xl mx-auto bg-white rounded-xl shadow-2xl overflow-hidden">
      <div className="bg-gradient-to-r from-blue-600 to-indigo-600 px-6 py-4">
        <div className="flex items-center justify-between text-white">
          <h2 className="text-xl font-semibold">Configuration Wizard</h2>
          <span className="text-sm bg-white/20 px-3 py-1 rounded-full">Step {page + 1} of 6</span>
        </div>
      </div>
      <div className="p-8 min-h-[400px]">
        {page === 0 && <ComponentTypePanel value={componentType} onChange={setComponentType} />}
        {page === 1 && <NamePanel value={name} onChange={setName} />}
        {page === 2 && <MeshDimensionsPanel width={width} height={height} onChange={(field, val) => field === 'width' ? setWidth(val) : setHeight(val)} />}
        {page === 3 && <RoutingPanel value={routingAlgorithm} onChange={setRoutingAlgorithm} />}
        {page === 4 && <NocParamsPanel vcCount={vcCount} bufferDepth={bufferDepth} onChange={(field, val) => field === 'vcCount' ? setVcCount(val) : setBufferDepth(val)} />}
        {page === 5 && <EndpointPanel endpoints={endpoints} crosspoints={crosspoints} onAdd={handleAddEndpoint} onRemove={handleRemoveEndpoint} />}
      </div>
      <div className="bg-gray-50 px-8 py-4 flex justify-between border-t">
        <button
          onClick={() => setPage(page - 1)}
          disabled={page === 0}
          className="px-6 py-2 bg-gray-200 text-gray-700 rounded-lg hover:bg-gray-300 disabled:opacity-50 disabled:cursor-not-allowed transition-colors font-medium"
        >
          Previous
        </button>
        <div className="flex gap-3">
          <button
            onClick={handleCancel}
            className="px-6 py-2 bg-gray-200 text-gray-700 rounded-lg hover:bg-gray-300 transition-colors font-medium"
          >
            Cancel
          </button>
          <button
            onClick={handleNext}
            disabled={!isPageValid()}
            className="px-6 py-2 bg-blue-600 text-white rounded-lg hover:bg-blue-700 disabled:opacity-50 disabled:cursor-not-allowed transition-colors font-medium"
          >
            {page === 5 ? 'Finish' : 'Next'}
          </button>
        </div>
      </div>
    </div>
  );
}

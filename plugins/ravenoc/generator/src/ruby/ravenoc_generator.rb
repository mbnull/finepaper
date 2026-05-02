require 'erb'
require 'fileutils'
require 'json'
require 'open3'

class RaveNoCGenerator
  class GenerationError < StandardError; end

  REQUIRED_VENDOR_FILES = [
    'bus_arch_sv_pkg/amba_axi_pkg.sv',
    'src/include/ravenoc_axi_fnc.svh',
    'src/include/ravenoc_defines.svh',
    'src/include/ravenoc_structs.svh',
    'src/include/ravenoc_pkg.sv',
    'src/ni/axi_csr.sv',
    'src/ni/axi_slave_if.sv',
    'src/ni/router_wrapper.sv',
    'src/ni/async_gp_fifo.sv',
    'src/ni/cdc_pkt.sv',
    'src/ni/pkt_proc.sv',
    'src/router/fifo.sv',
    'src/router/output_module.sv',
    'src/router/router_if.sv',
    'src/router/router_ravenoc.sv',
    'src/router/rr_arbiter.sv',
    'src/router/vc_buffer.sv',
    'src/router/input_router.sv',
    'src/router/input_module.sv',
    'src/router/input_datapath.sv',
    'src/ravenoc.sv'
  ].freeze

  VENDOR_SOURCE_FILES = [
    'bus_arch_sv_pkg/amba_axi_pkg.sv',
    'src/include/ravenoc_pkg.sv',
    'src/ni/axi_csr.sv',
    'src/ni/axi_slave_if.sv',
    'src/ni/router_wrapper.sv',
    'src/ni/async_gp_fifo.sv',
    'src/ni/cdc_pkt.sv',
    'src/ni/pkt_proc.sv',
    'src/router/fifo.sv',
    'src/router/output_module.sv',
    'src/router/router_if.sv',
    'src/router/router_ravenoc.sv',
    'src/router/rr_arbiter.sv',
    'src/router/vc_buffer.sv',
    'src/router/input_router.sv',
    'src/router/input_module.sv',
    'src/router/input_datapath.sv',
    'src/ravenoc.sv'
  ].freeze

  DEFAULTS = {
    'rows' => 2,
    'cols' => 2,
    'flit_data_width' => 32,
    'flit_type_width' => 2,
    'flit_buffer_depth' => 2,
    'virtual_channels' => 3,
    'routing_algorithm' => 'xy',
    'priority' => 'zero_high',
    'max_packet_flits' => 256,
    'axi_addr_width' => 32,
    'axi_data_width' => 32,
    'axi_cdc_required' => 'all',
    'bypass_cdc' => false
  }.freeze

  ROUTING_MAP = {
    'xy' => 'XYAlg',
    'yx' => 'YXAlg'
  }.freeze

  PRIORITY_MAP = {
    'zero_high' => 'ZeroHighPrior',
    'zero_low' => 'ZeroLowPrior'
  }.freeze

  DEFINE_NAMES = {
    'rows' => 'NOC_CFG_SZ_ROWS',
    'cols' => 'NOC_CFG_SZ_COLS',
    'flit_data_width' => 'FLIT_DATA_WIDTH',
    'flit_type_width' => 'FLIT_TP_WIDTH',
    'flit_buffer_depth' => 'FLIT_BUFF',
    'virtual_channels' => 'N_VIRT_CHN',
    'routing_algorithm' => 'ROUTING_ALG',
    'priority' => 'H_PRIORITY',
    'max_packet_flits' => 'MAX_SZ_PKT',
    'axi_addr_width' => 'AXI_ADDR_WIDTH',
    'axi_data_width' => 'AXI_DATA_WIDTH'
  }.freeze

  attr_reader :input_path, :output_dir, :template_dir, :vendor_dir

  def initialize(input_path:, output_dir:, template_dir:, vendor_dir:)
    @input_path = input_path
    @output_dir = output_dir
    @template_dir = template_dir
    @vendor_dir = vendor_dir
  end

  def generate
    graph = read_graph
    module_record = single_ravenoc_module(graph)
    parameters = DEFAULTS.merge(module_record.fetch('parameters', {}))
    validate_vendor!
    validate_parameters!(parameters)

    FileUtils.mkdir_p(output_dir)
    template_binding = binding_for(module_record, parameters)
    render('ravenoc_config.svh.erb', File.join(output_dir, 'ravenoc_config.svh'), template_binding)
    render('ravenoc_demo_top.sv.erb', File.join(output_dir, 'ravenoc_demo_top.sv'), template_binding)
    render('ravenoc_filelist.f.erb', File.join(output_dir, 'ravenoc_filelist.f'), template_binding)
    render('verify.sh.erb', File.join(output_dir, 'verify.sh'), template_binding)
    FileUtils.chmod(0o755, File.join(output_dir, 'verify.sh'))
    write_manifest(module_record, parameters)
    puts "Generated RaveNoC integration in #{output_dir}"
  end

  private

  def read_graph
    data = JSON.parse(File.read(input_path))
    raise GenerationError, 'expected schema finepaper-plugin-graph-v1' unless data['schema'] == 'finepaper-plugin-graph-v1'

    data
  rescue Errno::ENOENT
    raise GenerationError, "input graph not found: #{input_path}"
  rescue JSON::ParserError => error
    raise GenerationError, "invalid JSON input: #{error.message}"
  end

  def single_ravenoc_module(graph)
    modules = graph.fetch('modules', []).select do |mod|
      mod['plugin'] == 'finepaper.ravenoc' && mod['type'] == 'RaveNoC'
    end
    raise GenerationError, "expected exactly one RaveNoC module, found #{modules.size}" unless modules.size == 1

    modules.first
  end

  def validate_vendor!
    missing = REQUIRED_VENDOR_FILES.find { |relative| !File.file?(File.join(vendor_dir, relative)) }
    return unless missing

    raise GenerationError,
          "RaveNoC vendor source is missing or incomplete. Run: git submodule update --init --recursive. Missing: #{missing}"
  end

  def positive_integer!(parameters, name)
    value = parameters[name]
    raise GenerationError, "#{name} must be a positive integer" unless value.is_a?(Integer) && value.positive?

    value
  end

  def validate_parameters!(parameters)
    rows = positive_integer!(parameters, 'rows')
    cols = positive_integer!(parameters, 'cols')
    raise GenerationError, '1x1 is not a legal RaveNoC mesh' if rows == 1 && cols == 1

    buffer_depth = positive_integer!(parameters, 'flit_buffer_depth')
    raise GenerationError, 'flit_buffer_depth must be a power of two' unless (buffer_depth & (buffer_depth - 1)).zero?

    %w[flit_data_width flit_type_width virtual_channels max_packet_flits axi_addr_width axi_data_width].each do |name|
      positive_integer!(parameters, name)
    end
    raise GenerationError, 'routing_algorithm must be xy or yx' unless ROUTING_MAP.key?(parameters['routing_algorithm'])
    raise GenerationError, 'priority must be zero_high or zero_low' unless PRIORITY_MAP.key?(parameters['priority'])
    validate_axi_cdc_required!(parameters)
  end

  def validate_axi_cdc_required!(parameters)
    noc_size = parameters.fetch('rows') * parameters.fetch('cols')
    value = parameters.fetch('axi_cdc_required', 'all').to_s.strip.downcase.delete('_')
    return if %w[all none].include?(value)
    return if value.match?(/\A[01]+\z/) && value.length == noc_size

    raise GenerationError, "axi_cdc_required must be all, none, or a #{noc_size}-bit binary mask"
  end

  def define_values(parameters)
    DEFINE_NAMES.to_h do |parameter_name, define_name|
      value = parameters.fetch(parameter_name)
      value = ROUTING_MAP.fetch(value) if parameter_name == 'routing_algorithm'
      value = PRIORITY_MAP.fetch(value) if parameter_name == 'priority'
      [define_name, value]
    end
  end

  def axi_cdc_literal(parameters)
    noc_size = parameters.fetch('rows') * parameters.fetch('cols')
    value = parameters.fetch('axi_cdc_required', 'all').to_s.strip.downcase
    return "{#{noc_size}{1'b1}}" if value == 'all'
    return "{#{noc_size}{1'b0}}" if value == 'none'

    "#{noc_size}'b#{value.delete('_')}"
  end

  def bypass_cdc_literal(parameters)
    parameters.fetch('bypass_cdc') ? "1'b1" : "1'b0"
  end

  def vendor_files
    VENDOR_SOURCE_FILES
  end

  def render(template_name, output_path, template_binding)
    template = File.read(File.join(template_dir, template_name))
    File.write(output_path, ERB.new(template, trim_mode: '-').result(template_binding))
  end

  def binding_for(module_record, parameters)
    define_values = define_values(parameters)
    axi_cdc_literal = axi_cdc_literal(parameters)
    bypass_cdc_literal = bypass_cdc_literal(parameters)
    vendor_files = vendor_files()
    output_dir = self.output_dir
    vendor_dir = self.vendor_dir
    binding
  end

  def source_commit
    stdout, _stderr, status = Open3.capture3('git', '-C', vendor_dir, 'rev-parse', 'HEAD')
    status.success? ? stdout.strip : 'unknown'
  rescue StandardError
    'unknown'
  end

  def write_manifest(module_record, parameters)
    manifest = {
      plugin: 'finepaper.ravenoc',
      source: {
        repository: 'https://github.com/aignacio/ravenoc.git',
        commit: source_commit
      },
      module: {
        id: module_record['id'],
        type: module_record['type']
      },
      parameters: parameters,
      verification: {
        command: 'bash verify.sh'
      }
    }
    File.write(File.join(output_dir, 'manifest.json'), "#{JSON.pretty_generate(manifest)}\n")
  end
end

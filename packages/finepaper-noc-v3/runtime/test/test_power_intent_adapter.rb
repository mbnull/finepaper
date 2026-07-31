# frozen_string_literal: true

require 'json'
require 'fileutils'
require 'minitest/autorun'
require 'open3'
require 'tmpdir'
require_relative '../lib/power_intent/control_port_preflight'

class PowerIntentAdapterTest < Minitest::Test
  PACKAGE_ROOT = File.expand_path('../..', __dir__)
  ADAPTER = File.join(PACKAGE_ROOT, 'runtime', 'bin', 'generate')
  PACKAGE_MANIFEST = File.join(PACKAGE_ROOT, 'package.json')
  EXTENSION_ID = 'finepaper.noc.powerIntent'
  EXTENSION_PATH = "/design/packageData/#{EXTENSION_ID}"

  def setup
    @temporary_directories = []
  end

  def teardown
    @temporary_directories.each do |directory|
      FileUtils.remove_entry(directory) if Dir.exist?(directory)
    end
  end

  def test_package_declares_the_versioned_power_intent_extension
    package = JSON.parse(File.read(PACKAGE_MANIFEST))
    declaration = package.fetch('designExtensions').find do |entry|
      entry.fetch('id') == EXTENSION_ID
    end

    refute_nil declaration
    assert_equal 'runtime/power-intent.schema.json', declaration.fetch('schema')
    assert_equal 1, declaration.fetch('version')
  end

  def test_validate_remains_compatible_when_the_extension_is_absent
    result = run_adapter('validate', minimal_design)

    assert result.fetch(:status).success?, result.fetch(:log)
    assert_equal true, result.dig(:result, 'success')
    assert_empty result.dig(:result, 'diagnostics')
    assert_empty result.dig(:result, 'artifacts')
  end

  def test_generate_without_the_extension_does_not_invent_power_artifacts
    result = run_adapter('generate', minimal_design)

    assert result.fetch(:status).success?, result.fetch(:log)
    power_types = result.dig(:result, 'artifacts').filter_map do |entry|
      entry.fetch('type') if entry.fetch('type').start_with?('power-')
    end
    assert_empty power_types
  end

  def test_regeneration_without_the_extension_removes_stale_power_artifacts
    directory = Dir.mktmpdir('finepaper-power-intent-regeneration-')
    with_power = run_adapter(
      'generate', design_with_power_intent, directory: directory
    )
    without_power = run_adapter('generate', minimal_design, directory: directory)

    assert with_power.fetch(:status).success?, with_power.fetch(:log)
    assert without_power.fetch(:status).success?, without_power.fetch(:log)
    power_artifacts = without_power.dig(:result, 'artifacts').select do |entry|
      entry.fetch('type').start_with?('power-')
    end
    assert_empty power_artifacts
    %w[
      power_intent_plan.json
      power_implementation.json
      power_intent.upf
      power_intent_evidence.json
    ].each do |suffix|
      refute File.exist?(File.join(
        without_power.fetch(:output), "power_adapter_#{suffix}"
      ))
    end
  end

  def test_validate_preserves_compiler_diagnostics_under_the_extension_path
    design = design_with_power_intent
    design.dig('packageData', EXTENSION_ID)['bad/~field'] = true
    result = run_adapter('validate', design)

    refute result.fetch(:status).success?
    assert_equal false, result.dig(:result, 'success')
    diagnostic = result.dig(:result, 'diagnostics').fetch(0)
    assert_equal 'power_intent.unknown_field', diagnostic.fetch('code')
    assert_equal "#{EXTENSION_PATH}/bad~1~0field", diagnostic.fetch('path')
  end

  def test_validate_and_generate_preserve_control_namespace_diagnostics
    cases = [
      ['rst_n', 'top-port', 'rtl_hierarchy.logic_control_port_collision'],
      ['DATA_WIDTH', 'top-port',
       'rtl_hierarchy.logic_control_port_collision'],
      ['wire', 'top-port',
       'rtl_hierarchy.reserved_logic_control_identifier'],
      ['rst_n', 'upf-port', 'rtl_hierarchy.logic_control_port_collision'],
      ['wire', 'upf-port',
       'rtl_hierarchy.reserved_logic_control_identifier']
    ]

    cases.each do |signal, source, expected_code|
      diagnostics = %w[validate generate].map do |command|
        design = design_with_power_intent
        design.dig('packageData', EXTENSION_ID, 'controls') << {
          'id' => 'conflicting-control',
          'signal' => signal,
          'source' => source,
          'activeSense' => 'high'
        }
        result = run_adapter(command, design)

        refute result.fetch(:status).success?, result.fetch(:log)
        assert_equal false, result.dig(:result, 'success')
        result.dig(:result, 'diagnostics').fetch(0)
      end

      diagnostics.each do |diagnostic|
        assert_equal expected_code, diagnostic.fetch('code')
        assert_equal "#{EXTENSION_PATH}/controls/0/signal",
                     diagnostic.fetch('path')
      end
      assert_equal diagnostics.fetch(0).slice('code', 'path'),
                   diagnostics.fetch(1).slice('code', 'path')
    end
  end

  def test_control_diagnostics_point_to_the_original_unsorted_array
    %w[rst_n DATA_WIDTH].each do |signal|
      %w[validate generate].each do |command|
        design = design_with_power_intent
        controls = design.dig('packageData', EXTENSION_ID, 'controls')
        controls.concat([
          {
            'id' => 'a-conflicting-control',
            'signal' => signal,
            'source' => 'top-port',
            'activeSense' => 'high'
          },
          {
            'id' => 'z-legal-control',
            'signal' => 'legal_control',
            'source' => 'top-port',
            'activeSense' => 'high'
          }
        ]).reverse!

        result = run_adapter(command, design)

        refute result.fetch(:status).success?, result.fetch(:log)
        diagnostic = result.dig(:result, 'diagnostics').fetch(0)
        assert_equal 'rtl_hierarchy.logic_control_port_collision',
                     diagnostic.fetch('code')
        assert_equal "#{EXTENSION_PATH}/controls/1/signal",
                     diagnostic.fetch('path')
      end
    end
  end

  def test_validate_accepts_a_legal_top_port_control
    design = design_with_power_intent
    design.dig('packageData', EXTENSION_ID, 'controls') << {
      'id' => 'isolation-request',
      'signal' => 'isolation_request',
      'source' => 'top-port',
      'activeSense' => 'high'
    }

    result = run_adapter('validate', design)

    assert result.fetch(:status).success?, result.fetch(:log)
    assert_equal true, result.dig(:result, 'success')
    assert_empty result.dig(:result, 'diagnostics')
  end

  def test_top_namespace_rejects_endpoint_port_and_instance_aliases_deterministically
    context = Object.new
    context.define_singleton_method(:domains_for_role) { |_role| [] }
    context.define_singleton_method(:entities) { {} }
    context.define_singleton_method(:edges) { {} }
    endpoint_paths = {
      'u_ni_x' => '/design/endpoints/0/id',
      'x_flit_in' => '/design/endpoints/1/id'
    }
    endpoint_orders = [
      {'u_ni_x' => nil, 'x_flit_in' => nil},
      {'x_flit_in' => nil, 'u_ni_x' => nil}
    ]

    errors = endpoint_orders.map do |endpoint_ports|
      assert_raises(FinepaperNoc::PowerIntent::ControlPortPreflightError) do
        FinepaperNoc::PowerIntent::ControlPortPreflight.top_namespace(
          context: context,
          endpoint_ports: endpoint_ports,
          endpoint_paths_by_id: endpoint_paths
        )
      end
    end

    errors.each do |error|
      assert_equal 'rtl_hierarchy.top_namespace_collision', error.code
      assert_equal '/design/endpoints/1/id', error.path
      refute error.power_intent_relative?
      assert_includes error.detail, 'u_ni_x_flit_in'
      assert_includes error.detail, 'Endpoint u_ni_x'
      assert_includes error.detail, 'Endpoint x_flit_in instance'
    end
    assert_equal errors.fetch(0).detail, errors.fetch(1).detail
  end

  def test_validate_and_generate_reject_endpoint_namespace_aliases_outside_power_intent
    runs = %w[validate generate].map do |command|
      result = run_adapter(command, design_with_endpoint_namespace_collision)

      refute result.fetch(:status).success?, result.fetch(:log)
      assert_equal false, result.dig(:result, 'success')
      refute Dir.exist?(result.fetch(:output)) if command == 'generate'
      result
    end

    diagnostics = runs.map do |result|
      result.dig(:result, 'diagnostics').fetch(0)
    end
    diagnostics.each do |diagnostic|
      assert_equal 'rtl_hierarchy.top_namespace_collision',
                   diagnostic.fetch('code')
      assert_equal '/design/endpoints/1/id', diagnostic.fetch('path')
      refute diagnostic.fetch('path').start_with?(EXTENSION_PATH)
      assert_includes diagnostic.fetch('message'), 'u_ni_x_flit_in'
    end
    assert_equal diagnostics.fetch(0).slice('code', 'path', 'message'),
                 diagnostics.fetch(1).slice('code', 'path', 'message')
  end

  def test_validate_and_generate_reject_active_use_of_an_inactive_switched_rail
    diagnostics = %w[validate generate].map do |command|
      result = run_adapter(command, design_with_inactive_switch_driver)

      refute result.fetch(:status).success?, result.fetch(:log)
      assert_equal false, result.dig(:result, 'success')
      result.dig(:result, 'diagnostics').fetch(0)
    end

    diagnostics.each do |diagnostic|
      assert_equal 'power_intent.invalid_always_on_primary_supply',
                   diagnostic.fetch('code')
      assert_equal "#{EXTENSION_PATH}/domains/0/primaryPower",
                   diagnostic.fetch('path')
    end
    assert_equal diagnostics.fetch(0).slice('code', 'path'),
                 diagnostics.fetch(1).slice('code', 'path')
  end

  def test_generate_emits_a_deterministic_canonical_plan_artifact
    first = run_adapter('generate', design_with_power_intent)
    reordered_design = design_with_power_intent
    reordered_design.dig('packageData', EXTENSION_ID, 'supplies').reverse!
    second = run_adapter('generate', reordered_design)

    assert first.fetch(:status).success?, first.fetch(:log)
    assert second.fetch(:status).success?, second.fetch(:log)
    first_artifact = artifact(first, 'power-intent-plan')
    second_artifact = artifact(second, 'power-intent-plan')
    refute_nil first_artifact
    refute_nil second_artifact
    assert_equal 'power_adapter_power_intent_plan.json', first_artifact.fetch('path')
    assert_equal false, first_artifact.fetch('primary')
    assert_equal first_artifact, second_artifact

    first_text = File.read(File.join(first.fetch(:output), first_artifact.fetch('path')))
    second_text = File.read(File.join(second.fetch(:output), second_artifact.fetch('path')))
    assert_equal first_text, second_text
    assert first_text.end_with?("\n")
    plan = JSON.parse(first_text)
    assert_equal 'finepaper.noc-power-intent-plan', plan.fetch('format')
    assert_equal 1, plan.fetch('formatVersion')
    assert_equal 'power_adapter', plan.fetch('design')
    assert_equal %w[vdd vss], plan.fetch('supplies').map { |entry| entry.fetch('id') }
    assert_equal ['power-main'], plan.fetch('domains').map { |entry| entry.fetch('domain') }

    implementation_artifact = artifact(first, 'power-implementation-plan')
    upf_artifact = artifact(first, 'power-intent-upf')
    evidence_artifact = artifact(first, 'power-intent-evidence')
    refute_nil implementation_artifact
    refute_nil upf_artifact
    refute_nil evidence_artifact
    assert_equal 'power_adapter_power_implementation.json',
                 implementation_artifact.fetch('path')
    assert_equal 'power_adapter_power_intent.upf', upf_artifact.fetch('path')
    assert_equal 'power_adapter_power_intent_evidence.json',
                 evidence_artifact.fetch('path')

    implementation_text = File.read(File.join(
      first.fetch(:output), implementation_artifact.fetch('path')
    ))
    upf_text = File.read(File.join(
      first.fetch(:output), upf_artifact.fetch('path')
    ))
    evidence_text = File.read(File.join(
      first.fetch(:output), evidence_artifact.fetch('path')
    ))
    second_implementation = artifact(second, 'power-implementation-plan')
    second_upf = artifact(second, 'power-intent-upf')
    second_evidence = artifact(second, 'power-intent-evidence')
    assert_equal implementation_text, File.read(File.join(
      second.fetch(:output), second_implementation.fetch('path')
    ))
    assert_equal upf_text, File.read(File.join(
      second.fetch(:output), second_upf.fetch('path')
    ))
    assert_equal evidence_text, File.read(File.join(
      second.fetch(:output), second_evidence.fetch('path')
    ))
    assert_equal 'finepaper.noc-power-implementation-plan',
                 JSON.parse(implementation_text).fetch('format')
    assert_includes upf_text, 'upf_version 2.1'
    assert_includes upf_text, 'create_power_domain'
    evidence = JSON.parse(evidence_text)
    assert_equal 'finepaper.noc-power-intent-render-receipt',
                 evidence.fetch('format')
    assert_equal 'not-performed',
                 evidence.dig('validation', 'commercialSemanticValidation')
  end

  def test_generate_materializes_only_declared_top_port_controls_in_rtl
    design = design_with_power_intent
    design.dig('packageData', EXTENSION_ID, 'controls').concat([
      {
        'id' => 'isolation-request',
        'signal' => 'isolation_request',
        'source' => 'top-port',
        'activeSense' => 'high'
      },
      {
        'id' => 'tool-only-request',
        'signal' => 'tool_only_request',
        'source' => 'upf-port',
        'activeSense' => 'low'
      }
    ])

    result = run_adapter('generate', design)

    assert result.fetch(:status).success?, result.fetch(:log)
    top = File.read(File.join(result.fetch(:output), 'power_adapter_top.v'))
    assert_match(/input\s+logic\s+isolation_request\b/, top)
    refute_match(/input\s+logic\s+tool_only_request\b/, top)

    hierarchy_artifact = artifact(result, 'rtl-hierarchy')
    refute_nil hierarchy_artifact
    hierarchy = JSON.parse(File.read(File.join(
      result.fetch(:output), hierarchy_artifact.fetch('path')
    )))
    assert_equal [
      {
        'direction' => 'input',
        'id' => 'isolation-request',
        'signal' => 'isolation_request',
        'source' => 'top-port'
      }
    ], hierarchy.fetch('logicControlPorts')
  end

  def test_generate_realizes_direct_power_boundaries_and_defers_combined_cdc
    result = run_adapter('generate', mixed_boundary_design)

    assert result.fetch(:status).success?, result.fetch(:log)
    implementation_artifact = artifact(result, 'power-implementation-plan')
    upf_artifact = artifact(result, 'power-intent-upf')
    evidence_artifact = artifact(result, 'power-intent-evidence')
    implementation = JSON.parse(File.read(File.join(
      result.fetch(:output), implementation_artifact.fetch('path')
    )))
    upf = File.read(File.join(
      result.fetch(:output), upf_artifact.fetch('path')
    ))
    evidence = JSON.parse(File.read(File.join(
      result.fetch(:output), evidence_artifact.fetch('path')
    )))

    direct = implementation.fetch('edgeOrientations').select do |entry|
      entry.dig('edge', 'kind') == 'endpoint-attachment' &&
        entry.dig('edge', 'id') == 'ep_direct'
    end
    combined = implementation.fetch('edgeOrientations').select do |entry|
      entry.dig('edge', 'kind') == 'router-link'
    end
    assert_equal 2, direct.size
    assert_equal 2, combined.size
    assert direct.all? { |entry| entry.dig('powerBoundary', 'status') == 'resolvable' }
    assert combined.all? { |entry| entry.dig('powerBoundary', 'status') == 'deferred' }

    emitted = direct.flat_map { |entry| entry.fetch('signalFlows') }
                    .flat_map { |flow| [flow.fetch('isolation'), flow.fetch('levelShifter')] }
                    .select { |strategy| strategy.fetch('status') == 'expected' }
    deferred = combined.flat_map { |entry| entry.fetch('signalFlows') }
                       .flat_map { |flow| [flow.fetch('isolation'), flow.fetch('levelShifter')] }
    refute_empty emitted
    assert deferred.all? { |strategy| strategy.fetch('status') == 'deferred' }
    emitted.each { |strategy| assert_includes upf, strategy.fetch('token') }
    deferred.each { |strategy| refute_includes upf, strategy.fetch('token') }
    assert_includes upf, 'create_power_switch'
    assert_includes upf, 'set_retention'
    assert_includes upf, 'set_isolation'
    assert_includes upf, 'set_level_shifter'
    assert_equal false, evidence.dig('implementationCoverage', 'complete')
    assert_operator evidence.dig('implementationCoverage', 'summary', 'deferred'), :>, 0
    assert_equal 'emitted', evidence.dig('technology', 'bindingStatus')
    assert_equal 'not-performed',
                 evidence.dig('validation', 'commercialSemanticValidation')
  end

  def test_same_supply_cdc_defers_infrastructure_without_claiming_cell_bindings
    result = run_adapter('generate', same_supply_cdc_design)

    assert result.fetch(:status).success?, result.fetch(:log)
    implementation = JSON.parse(File.read(File.join(
      result.fetch(:output),
      artifact(result, 'power-implementation-plan').fetch('path')
    )))
    evidence = JSON.parse(File.read(File.join(
      result.fetch(:output),
      artifact(result, 'power-intent-evidence').fetch('path')
    )))
    upf = File.read(File.join(
      result.fetch(:output), artifact(result, 'power-intent-upf').fetch('path')
    ))

    router_directions = implementation.fetch('edgeOrientations').select do |entry|
      entry.dig('edge', 'kind') == 'router-link'
    end
    assert_equal 2, router_directions.size
    router_directions.each do |entry|
      assert_equal 'power-main', entry.fetch('sourceSupplyDomain')
      assert_equal 'power-main', entry.fetch('destinationSupplyDomain')
      assert_equal 'deferred', entry.dig('powerBoundary', 'status')
      assert_equal(
        'rtl_hierarchy.infrastructure_bridge_supply_unowned',
        entry.dig('powerBoundary', 'reasonCode')
      )
      assert entry.fetch('signalFlows').all? do |flow|
        flow.fetch('status') == 'deferred' &&
          flow.dig('isolation', 'status') == 'deferred' &&
          flow.dig('levelShifter', 'status') == 'deferred'
      end
    end

    ownership = implementation.dig('coverage', 'items').select do |item|
      item.fetch('kind') == 'infrastructure-supply-ownership'
    end
    reasons = ownership.map { |item| item.fetch('reason') }.tally
    assert_equal 2,
                 reasons.fetch(
                   'power_implementation.reset_synchronizer_supply_unowned'
                 )
    assert_equal 2,
                 reasons.fetch(
                   'rtl_hierarchy.infrastructure_bridge_supply_unowned'
                 )
    assert_equal false, evidence.dig('implementationCoverage', 'complete')
    assert_equal 'upf-interface-cells', evidence.dig('technology', 'profile')
    assert_equal 'not-required', evidence.dig('technology', 'bindingStatus')
    assert_equal 0, evidence.dig('technology', 'bindingCommandCount')
    binding_kinds = %w[
      map_power_switch_cell
      map_retention_cell
      use_interface_cell
    ]
    binding_commands = evidence.fetch('commands').select do |command|
      binding_kinds.include?(command.fetch('kind'))
    end
    assert_empty binding_commands
    binding_kinds.each { |kind| refute_match(/^#{kind}\b/, upf) }
  end

  def test_generate_fails_before_artifacts_when_the_declared_extension_is_null
    design = minimal_design
    design['packageData'] = {EXTENSION_ID => nil}
    result = run_adapter('generate', design)

    refute result.fetch(:status).success?
    assert_equal false, result.dig(:result, 'success')
    diagnostic = result.dig(:result, 'diagnostics').fetch(0)
    assert_equal 'power_intent.expected_object', diagnostic.fetch('code')
    assert_equal EXTENSION_PATH, diagnostic.fetch('path')
    refute Dir.exist?(result.fetch(:output))
  end

  private

  def artifact(run, type)
    run.dig(:result, 'artifacts').find do |entry|
      entry.fetch('type') == type
    end
  end

  def run_adapter(command, design, directory: nil)
    directory ||= Dir.mktmpdir('finepaper-power-intent-adapter-')
    @temporary_directories << directory unless @temporary_directories.include?(directory)
    design_path = File.join(directory, 'design.json')
    result_path = File.join(directory, 'result.json')
    output_path = File.join(directory, 'output')
    File.write(design_path, JSON.pretty_generate(design) + "\n")
    arguments = [
      'ruby', ADAPTER, command,
      '--design', design_path,
      '--result', result_path
    ]
    arguments.concat(['--output', output_path]) if command == 'generate'
    stdout, stderr, status = Open3.capture3(*arguments)
    {
      status: status,
      log: "#{stdout}#{stderr}",
      result: JSON.parse(File.read(result_path)),
      output: output_path
    }
  end

  def design_with_power_intent
    minimal_design.merge(
      'packageData' => {EXTENSION_ID => power_intent}
    )
  end

  def design_with_endpoint_namespace_collision
    design = minimal_design
    endpoint_ids = %w[u_ni_x x_flit_in]
    design['endpoints'] = endpoint_ids.map { |id| endpoint(id, 0) }
    elements = [{'kind' => 'router', 'id' => 'r-0-0'}] +
      endpoint_ids.map { |id| {'kind' => 'endpoint', 'id' => id} }
    design['domainMemberships'] = elements.map do |element|
      {
        'element' => element,
        'assignments' => {
          'clock' => ['clock-main'],
          'power' => ['power-main']
        }
      }
    end
    design
  end

  def power_intent
    {
      'format' => 'finepaper.noc-power-intent',
      'formatVersion' => 1,
      'supplies' => [
        {
          'id' => 'vss',
          'kind' => 'ground',
          'exposure' => 'external-port',
          'port' => 'VSS',
          'net' => 'VSS',
          'states' => [
            {'id' => 'on', 'condition' => 'full-on', 'voltageMv' => 0}
          ]
        },
        {
          'id' => 'vdd',
          'kind' => 'power',
          'exposure' => 'external-port',
          'port' => 'VDD',
          'net' => 'VDD',
          'states' => [
            {'id' => 'on', 'condition' => 'full-on', 'voltageMv' => 900}
          ]
        }
      ],
      'controls' => [],
      'domains' => [
        {
          'domain' => 'power-main',
          'primaryPower' => 'vdd',
          'primaryGround' => 'vss',
          'mode' => 'always-on',
          'defaultState' => 'on',
          'states' => [
            {
              'id' => 'on',
              'powerState' => 'on',
              'groundState' => 'on',
              'behavior' => 'operational'
            }
          ]
        }
      ],
      'defaultSystemState' => 'run',
      'systemStates' => [
        {
          'id' => 'run',
          'domainStates' => [{'domain' => 'power-main', 'state' => 'on'}]
        }
      ]
    }
  end

  def minimal_design
    elements = [
      {'kind' => 'router', 'id' => 'r-0-0'},
      {'kind' => 'endpoint', 'id' => 'ep0'}
    ]
    {
      'format' => 'finepaper.noc-design',
      'formatVersion' => 3,
      'id' => 'power_adapter',
      'name' => 'Power Adapter',
      'package' => {'id' => 'finepaper.noc', 'version' => '3.1.0'},
      'topology' => {'type' => 'mesh', 'rows' => 1, 'columns' => 1},
      'parameters' => {'dataWidth' => 64, 'flitWidth' => 128, 'addrWidth' => 32},
      'endpoints' => [
        {
          'id' => 'ep0',
          'type' => 'master',
          'attachment' => {'router' => {'x' => 0, 'y' => 0}},
          'parameters' => {
            'protocol' => 'axi4',
            'dataWidth' => 64,
            'bufferDepth' => 16,
            'qosEnabled' => false
          }
        }
      ],
      'domains' => [
        {
          'id' => 'clock-main',
          'type' => 'clock',
          'name' => 'Clock',
          'properties' => {'frequencyMHz' => 1000, 'resetReleaseStages' => 2}
        },
        {
          'id' => 'power-main',
          'type' => 'power',
          'name' => 'Power',
          'properties' => {'voltageMv' => 900, 'retention' => false}
        }
      ],
      'domainMemberships' => elements.map do |element|
        {
          'element' => element,
          'assignments' => {
            'clock' => ['clock-main'],
            'power' => ['power-main']
          }
        }
      end,
      'domainRelations' => [],
      'crossingPolicies' => [],
      'edgeOverrides' => [],
      'elementConfigurations' => []
    }
  end


  def mixed_boundary_design
    elements = [
      {'kind' => 'router', 'id' => 'r-0-0'},
      {'kind' => 'router', 'id' => 'r-1-0'},
      {'kind' => 'endpoint', 'id' => 'ep_direct'},
      {'kind' => 'endpoint', 'id' => 'ep_combined'}
    ]
    assignments = {
      ['router', 'r-0-0'] => %w[clock-main power-main],
      ['router', 'r-1-0'] => %w[clock-io power-low],
      ['endpoint', 'ep_direct'] => %w[clock-main power-low],
      ['endpoint', 'ep_combined'] => %w[clock-io power-low]
    }
    {
      'format' => 'finepaper.noc-design',
      'formatVersion' => 3,
      'id' => 'mixed_power',
      'name' => 'Mixed Power Boundaries',
      'package' => {'id' => 'finepaper.noc', 'version' => '3.1.0'},
      'topology' => {'type' => 'mesh', 'rows' => 1, 'columns' => 2},
      'parameters' => {'dataWidth' => 64, 'flitWidth' => 128, 'addrWidth' => 32},
      'endpoints' => [
        endpoint('ep_direct', 0),
        endpoint('ep_combined', 1)
      ],
      'domains' => [
        domain_definition('clock-main', 'clock', 1000, false),
        domain_definition('clock-io', 'clock', 500, false),
        domain_definition('power-main', 'power', 900, false),
        domain_definition('power-low', 'power', 750, true)
      ],
      'domainMemberships' => elements.map do |element|
        clock, power = assignments.fetch([element.fetch('kind'), element.fetch('id')])
        {
          'element' => element,
          'assignments' => {'clock' => [clock], 'power' => [power]}
        }
      end,
      'domainRelations' => [
        {
          'type' => 'derived-from', 'from' => 'clock-io', 'to' => 'clock-main',
          'properties' => {'divider' => 2}
        }
      ],
      'crossingPolicies' => [
        {
          'id' => 'clock-main-to-io', 'domainType' => 'clock',
          'from' => 'clock-main', 'to' => 'clock-io',
          'properties' => {
            'implementation' => 'async-fifo',
            'synchronizerStages' => 3,
            'fifoDepth' => 4
          }
        },
        {
          'id' => 'power-main-to-low', 'domainType' => 'power',
          'from' => 'power-main', 'to' => 'power-low',
          'properties' => {'isolation' => true, 'levelShift' => 'auto'}
        }
      ],
      'edgeOverrides' => [],
      'elementConfigurations' => [],
      'packageData' => {EXTENSION_ID => mixed_power_intent}
    }
  end

  def design_with_inactive_switch_driver
    design = mixed_boundary_design
    design.fetch('domains') <<
      domain_definition('power-unused', 'power', 900, false)
    intent = design.dig('packageData', EXTENSION_ID)
    intent.fetch('supplies').concat([
      power_supply('vdd-unused-in', 'VDD_UNUSED_IN', 'external-port', 900),
      power_supply('vdd-unused-sw', 'VDD_UNUSED_SW', 'internal-switched', 900,
                   switchable: true)
    ])
    intent.fetch('controls') <<
      power_control('unused-enable', 'unused_enable')
    intent.fetch('domains').find do |domain|
      domain.fetch('domain') == 'power-main'
    end['primaryPower'] = 'vdd-unused-sw'
    intent.fetch('domains') << {
      'domain' => 'power-unused',
      'primaryPower' => 'vdd-unused-sw', 'primaryGround' => 'vss',
      'mode' => 'switchable', 'defaultState' => 'on',
      'states' => [
        {
          'id' => 'on', 'powerState' => 'on', 'groundState' => 'on',
          'behavior' => 'operational'
        },
        {
          'id' => 'off', 'powerState' => 'off', 'groundState' => 'on',
          'behavior' => 'corrupt'
        }
      ],
      'powerSwitch' => {
        'inputSupply' => 'vdd-unused-in',
        'outputSupply' => 'vdd-unused-sw',
        'control' => 'unused-enable', 'onSense' => 'high'
      }
    }
    design
  end

  def same_supply_cdc_design
    design = mixed_boundary_design
    design.fetch('domainMemberships').each do |membership|
      membership.dig('assignments', 'power').replace(['power-main'])
    end
    design.fetch('crossingPolicies').select! do |policy|
      policy.fetch('domainType') == 'clock'
    end
    intent = design.dig('packageData', EXTENSION_ID)
    intent['systemStates'] = [{
      'id' => 'run',
      'domainStates' => [{'domain' => 'power-main', 'state' => 'on'}]
    }]
    design
  end

  def endpoint(id, x)
    {
      'id' => id,
      'type' => 'master',
      'attachment' => {'router' => {'x' => x, 'y' => 0}},
      'parameters' => {
        'protocol' => 'axi4', 'dataWidth' => 64,
        'bufferDepth' => 16, 'qosEnabled' => false
      }
    }
  end

  def domain_definition(id, type, value, retention)
    properties = if type == 'clock'
                   {'frequencyMHz' => value, 'resetReleaseStages' => 2}
                 else
                   {'voltageMv' => value, 'retention' => retention}
                 end
    {'id' => id, 'type' => type, 'name' => id, 'properties' => properties}
  end

  def mixed_power_intent
    {
      'format' => 'finepaper.noc-power-intent',
      'formatVersion' => 1,
      'supplies' => [
        power_supply('vdd-main', 'VDD_MAIN', 'external-port', 900),
        power_supply('vdd-low-in', 'VDD_LOW_IN', 'external-port', 750),
        power_supply('vdd-low-sw', 'VDD_LOW_SW', 'internal-switched', 750,
                     switchable: true),
        {
          'id' => 'vss', 'kind' => 'ground', 'exposure' => 'external-port',
          'port' => 'VSS', 'net' => 'VSS',
          'states' => [
            {'id' => 'on', 'condition' => 'full-on', 'voltageMv' => 0}
          ]
        }
      ],
      'controls' => [
        power_control('low-enable', 'low_enable'),
        power_control('low-isolate', 'low_isolate'),
        power_control('low-save', 'low_save'),
        power_control('low-restore', 'low_restore')
      ],
      'domains' => [
        {
          'domain' => 'power-main',
          'primaryPower' => 'vdd-main', 'primaryGround' => 'vss',
          'mode' => 'always-on', 'defaultState' => 'on',
          'states' => [
            {
              'id' => 'on', 'powerState' => 'on', 'groundState' => 'on',
              'behavior' => 'operational'
            }
          ],
          'levelShifter' => {'location' => 'automatic'}
        },
        {
          'domain' => 'power-low',
          'primaryPower' => 'vdd-low-sw', 'primaryGround' => 'vss',
          'mode' => 'switchable', 'defaultState' => 'on',
          'states' => [
            {
              'id' => 'on', 'powerState' => 'on', 'groundState' => 'on',
              'behavior' => 'operational'
            },
            {
              'id' => 'sleep', 'powerState' => 'off', 'groundState' => 'on',
              'behavior' => 'retained'
            }
          ],
          'powerSwitch' => {
            'inputSupply' => 'vdd-low-in', 'outputSupply' => 'vdd-low-sw',
            'control' => 'low-enable', 'onSense' => 'high'
          },
          'retention' => {
            'supply' => 'vdd-main', 'saveControl' => 'low-save',
            'restoreControl' => 'low-restore', 'saveEdge' => 'posedge',
            'restoreEdge' => 'negedge', 'location' => 'self'
          },
          'isolation' => {
            'control' => 'low-isolate', 'supply' => 'vdd-main',
            'clampValue' => 0, 'location' => 'parent'
          },
          'levelShifter' => {'location' => 'automatic'}
        }
      ],
      'defaultSystemState' => 'run',
      'systemStates' => [
        {
          'id' => 'run',
          'domainStates' => [
            {'domain' => 'power-main', 'state' => 'on'},
            {'domain' => 'power-low', 'state' => 'on'}
          ]
        },
        {
          'id' => 'sleep',
          'domainStates' => [
            {'domain' => 'power-main', 'state' => 'on'},
            {'domain' => 'power-low', 'state' => 'sleep'}
          ]
        }
      ],
      'technology' => {
        'profile' => 'upf-interface-cells',
        'interfaceCells' => [
          technology_cell('iso', 'isolation', 'LIB.ISO'),
          technology_cell('ls-up', 'level-shifter', 'LIB.LS_UP', 'up'),
          technology_cell('ls-down', 'level-shifter', 'LIB.LS_DOWN', 'down'),
          technology_cell('ret', 'retention', 'LIB.RET'),
          technology_cell('switch', 'power-switch', 'LIB.SWITCH')
        ]
      }
    }
  end

  def power_supply(id, net, exposure, voltage, switchable: false)
    supply = {
      'id' => id, 'kind' => 'power', 'exposure' => exposure, 'net' => net,
      'states' => [
        {'id' => 'on', 'condition' => 'full-on', 'voltageMv' => voltage}
      ]
    }
    supply['port'] = net if exposure == 'external-port'
    supply.fetch('states') << {'id' => 'off', 'condition' => 'off'} if switchable
    supply
  end

  def power_control(id, signal)
    {
      'id' => id, 'signal' => signal, 'source' => 'top-port',
      'activeSense' => 'high', 'ownerDomain' => 'power-main'
    }
  end

  def technology_cell(id, kind, cell, direction = nil)
    {'id' => id, 'kind' => kind, 'cells' => [cell]}.tap do |mapping|
      mapping['direction'] = direction if direction
    end
  end
end

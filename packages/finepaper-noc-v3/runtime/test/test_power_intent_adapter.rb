# frozen_string_literal: true

require 'json'
require 'fileutils'
require 'minitest/autorun'
require 'open3'
require 'tmpdir'

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

  def run_adapter(command, design)
    directory = Dir.mktmpdir('finepaper-power-intent-adapter-')
    @temporary_directories << directory
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
      'package' => {'id' => 'finepaper.noc', 'version' => '3.0.0'},
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
end

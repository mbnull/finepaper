# frozen_string_literal: true

require 'digest'
require 'json'
require_relative 'domain_rtl_context'

module FinepaperNoc
  class DomainRtlEvidenceError < StandardError
    attr_reader :code, :path

    def initialize(code, path, message)
      @code = code
      @path = path
      super("#{message} at #{path}")
    end
  end

  # Projects the validated implementation plan and the renderer's compiled
  # bridge records into deterministic, machine-checkable implementation facts.
  class DomainRtlEvidenceBuilder
    FORMAT = 'finepaper.noc-domain-implementation-evidence'
    VERSION = 1
    TIMING_ROLE = 'timing-domain'
    ASYNC_FIFO_RECIPE = 'clock-async-fifo'
    POWER_RECIPES = %w[power-isolation power-level-shifter].freeze
    ORIENTATIONS = DomainRtlContext::ORIENTATIONS
    EDGE_ORDER = {'router-link' => 0, 'endpoint-attachment' => 1}.freeze

    def self.build(**arguments)
      new(**arguments).build
    end

    def initialize(context:, domain_rendering:, top_module:, top_artifact:,
                   filelist_artifact:, source_plan_artifact:,
                   source_plan_contents:, async_reset_port: 'rst_n',
                   payload_width_parameter: 'FLIT_WIDTH')
      @context = context
      @rendering = domain_rendering
      @top_module = top_module
      @top_artifact = top_artifact
      @filelist_artifact = filelist_artifact
      @source_plan_artifact = source_plan_artifact
      @source_plan_contents = source_plan_contents
      @async_reset_port = async_reset_port
      @payload_width_parameter = payload_width_parameter
    end

    def build
      validate_inputs!
      infrastructure = compile_domain_infrastructure
      bridges = index_bridges
      realized_paths = {}
      edges = @context.plan.fetch('edgeBindings').each_with_index.map do |edge, index|
        compile_edge(edge, index, bridges, realized_paths)
      end.sort_by { |edge| edge_sort_key(edge.fetch('edge')) }
      expected_bridge_keys = edges.flat_map do |edge|
        ORIENTATIONS.map do |orientation|
          [edge.dig('edge', 'kind'), edge.dig('edge', 'id'), orientation]
        end
      end
      extra = bridges.keys - expected_bridge_keys
      expect!(extra.empty?, 'rtl_evidence.extra_bridge', '/domainRendering',
              "rendering contains extra bridge #{extra.first&.join(' ')}")
      deferred = compile_deferred_items(realized_paths)

      canonicalize({
        'format' => FORMAT,
        'formatVersion' => VERSION,
        'design' => @context.plan.fetch('design'),
        'sourcePlan' => {
          'format' => @context.plan.fetch('format'),
          'formatVersion' => @context.plan.fetch('formatVersion'),
          'artifact' => @source_plan_artifact,
          'sha256' => Digest::SHA256.hexdigest(@source_plan_contents)
        },
        'rtl' => {
          'topModule' => @top_module,
          'artifact' => @top_artifact,
          'filelist' => @filelist_artifact,
          'asyncResetPort' => @async_reset_port,
          'clockPortMode' => @rendering.fetch('clockPortMode', nil)
        }.compact,
        'claims' => {
          'timingDomainPorts' => true,
          'localResetRelease' => true,
          'clockAsyncFifo' => true,
          'completePlan' => deferred.empty?
        },
        'domainInfrastructure' => infrastructure,
        'edgeRealizations' => edges,
        'deferredPlanItems' => deferred,
        'summary' => {
          'usedTimingDomains' => infrastructure.size,
          'resetSynchronizers' => infrastructure.size,
          'physicalEdges' => edges.size,
          'asyncBoundaries' => edges.count { |edge| edge['status'] == 'realized' },
          'directionalFifos' => edges.sum { |edge| edge.fetch('directions').size },
          'directEdges' => edges.count { |edge| edge['status'] == 'direct' },
          'deferredPlanItems' => deferred.size
        }
      })
    end

    private

    def validate_inputs!
      expect!(@context.is_a?(DomainRtlContext), 'rtl_evidence.invalid_context',
              '/context', 'context must be a DomainRtlContext')
      expect!(@rendering.is_a?(Hash), 'rtl_evidence.invalid_rendering',
              '/domainRendering', 'domain rendering must be an object')
      [@top_artifact, @filelist_artifact, @source_plan_artifact].each do |path|
        valid = path.is_a?(String) && !path.empty? && !path.start_with?('/') &&
                !path.match?(/\A[A-Za-z]:\//) && !path.include?('\\') &&
                path.split('/').none? { |part| part.empty? || %w[. ..].include?(part) }
        expect!(valid, 'rtl_evidence.invalid_artifact_path', '/artifacts',
                'artifact paths must be contained relative POSIX paths')
      end
      parsed = JSON.parse(@source_plan_contents)
      expect!(parsed == @context.plan, 'rtl_evidence.plan_mismatch',
              '/sourcePlanContents',
              'source plan contents differ from the validated context')
    rescue JSON::ParserError => error
      fail!('rtl_evidence.invalid_plan_json', '/sourcePlanContents', error.message)
    end

    def compile_domain_infrastructure
      rendered = @rendering.fetch('clockDomains')
      active = @context.domains_for_role(TIMING_ROLE).reject do |domain|
        domain.fetch('members').empty?
      end
      by_id = rendered.to_h { |record| [record.fetch('domain'), record] }
      expect!(by_id.size == rendered.size && by_id.keys.sort ==
                active.map { |domain| domain.fetch('domain') }.sort,
              'rtl_evidence.clock_domain_mismatch',
              '/domainRendering/clockDomains',
              'rendered timing Domains differ from active plan Domains')
      domain_indices = @context.plan.fetch('domainBindings').each_with_index.to_h do |domain, index|
        [domain.fetch('domain'), index]
      end
      infrastructure = active.map do |domain|
        id = domain.fetch('domain')
        record = by_id.fetch(id)
        reset = domain.fetch('parameters').fetch('reset-release-stages')
        expect!(record.fetch('resetReleaseStages') == reset.fetch('value'),
                'rtl_evidence.reset_stage_mismatch',
                "/domainRendering/clockDomains/#{id}",
                'rendered reset depth differs from the plan')
        {
          'domain' => id,
          'name' => domain.fetch('name'),
          'token' => domain.fetch('token'),
          'planPath' => "/domainBindings/#{domain_indices.fetch(id)}",
          'clockPort' => record.fetch('clockSignal'),
          'asyncResetPort' => @async_reset_port,
          'localResetSignal' => record.fetch('resetSignal'),
          'module' => 'fp_reset_synchronizer',
          'instance' => "#{@top_module}.u_reset_#{domain.fetch('token')}",
          'parameters' => {
            'STAGES' => {
              'type' => 'integer', 'value' => reset.fetch('value'),
              'source' => copy(reset.fetch('source'))
            }
          }
        }
      end.sort_by { |entry| entry.fetch('domain') }
      @infrastructure_by_domain = infrastructure.to_h do |entry|
        [entry.fetch('domain'), entry]
      end
      infrastructure
    end

    def index_bridges
      values = @rendering.fetch('routerTraffic').values
      @rendering.fetch('endpointAttachments').keys.sort.each do |endpoint|
        attachment = @rendering.fetch('endpointAttachments').fetch(endpoint)
        values += %w[routerToEndpoint endpointToRouter].map do |key|
          attachment.fetch(key)
        end
      end
      values.each_with_object({}) do |bridge, result|
        edge = bridge.fetch('edge')
        key = [edge.fetch('kind'), edge.fetch('id'), bridge.fetch('orientation')]
        expect!(ORIENTATIONS.include?(key.last),
                'rtl_evidence.invalid_orientation', '/domainRendering',
                "unknown bridge orientation #{key.last}")
        expect!(!result.key?(key), 'rtl_evidence.duplicate_bridge',
                '/domainRendering', "duplicate bridge #{key.join(' ')}")
        result[key] = bridge
      end
    end

    def compile_edge(edge, edge_index, bridges, realized_paths)
      reference = edge.fetch('edge')
      key = [reference.fetch('kind'), reference.fetch('id')]
      oriented = ORIENTATIONS.map do |orientation|
        bridge = bridges[key + [orientation]]
        expect!(bridge, 'rtl_evidence.missing_bridge',
                "/edgeBindings/#{edge_index}",
                "edge #{reference.fetch('id')} lacks #{orientation} rendering")
        expected = @context.traffic(*key, orientation)
        expect!(bridge.fetch('producer') == expected.fetch('producer') &&
                  bridge.fetch('consumer') == expected.fetch('consumer'),
                'rtl_evidence.traffic_mismatch',
                "/edgeBindings/#{edge_index}",
                'rendered traffic direction differs from the plan')
        bridge
      end
      stage_index = edge.fetch('stages').index do |stage|
        stage_recipes(stage).include?(ASYNC_FIFO_RECIPE)
      end

      unless stage_index
        domains = oriented.flat_map do |bridge|
          [infrastructure_for(
             bridge.fetch('sourceDomain'), bridge.fetch('producer')
           ).fetch('domain'),
           infrastructure_for(
             bridge.fetch('destinationDomain'), bridge.fetch('consumer')
           ).fetch('domain')]
        end.uniq
        direct = oriented.all? do |bridge|
          !bridge.fetch('crossing') &&
            bridge.fetch('sourceSignal') == bridge.fetch('destinationSignal')
        end
        expect!(direct && domains.size == 1,
                'rtl_evidence.direct_domain_mismatch',
                "/edgeBindings/#{edge_index}",
                'direct edge rendering is not a same-Domain shared bundle')
        return {
          'edge' => copy(reference),
          'fromElement' => copy(edge.fetch('fromElement')),
          'toElement' => copy(edge.fetch('toElement')),
          'status' => 'direct',
          'planPath' => "/edgeBindings/#{edge_index}",
          'timingDomain' => domains.first,
          'directions' => []
        }
      end

      stage = edge.fetch('stages').fetch(stage_index)
      path = "/edgeBindings/#{edge_index}/stages/#{stage_index}"
      realized_paths[path] = true
      directions = oriented.map do |bridge|
        compile_direction(bridge, stage, path)
      end.sort_by { |direction| ORIENTATIONS.index(direction.fetch('orientation')) }
      {
        'edge' => copy(reference),
        'fromElement' => copy(edge.fetch('fromElement')),
        'toElement' => copy(edge.fetch('toElement')),
        'status' => 'realized',
        'planStage' => {
          'path' => path, 'order' => stage.fetch('order'),
          'role' => stage.fetch('role'), 'recipe' => ASYNC_FIFO_RECIPE
        },
        'directions' => directions
      }
    end

    def compile_direction(bridge, stage, path)
      expect!(bridge.fetch('crossing'), 'rtl_evidence.crossing_mismatch', path,
              'async FIFO stage was not rendered as a crossing')
      depth = @context.parameter_value(
        stage, 'fifo-depth', expected_type: 'integer'
      )
      sync_stages = @context.parameter_value(
        stage, 'metastability-stages', expected_type: 'integer'
      )
      expect!(bridge.fetch('fifoDepth') == depth,
              'rtl_evidence.fifo_depth_mismatch', path,
              'rendered FIFO depth differs from the plan')
      expect!(bridge.fetch('synchronizerStages') == sync_stages,
              'rtl_evidence.synchronizer_stage_mismatch', path,
              'rendered FIFO synchronizer depth differs from the plan')
      expect!(bridge.fetch('sourceSignal') != bridge.fetch('destinationSignal'),
              'rtl_evidence.crossing_signal_alias', path,
              'async FIFO source and destination bundles must be distinct')
      source = infrastructure_for(
        bridge.fetch('sourceDomain'), bridge.fetch('producer')
      )
      destination = infrastructure_for(
        bridge.fetch('destinationDomain'), bridge.fetch('consumer')
      )
      expect!(source.fetch('domain') != destination.fetch('domain'),
              'rtl_evidence.same_domain_fifo', path,
              'async FIFO cannot join one timing Domain')
      {
        'orientation' => bridge.fetch('orientation'),
        'producer' => copy(bridge.fetch('producer')),
        'consumer' => copy(bridge.fetch('consumer')),
        'module' => 'fp_async_ready_valid_fifo',
        'instance' => "#{@top_module}.#{bridge.fetch('instance')}",
        'parameters' => {
          'PAYLOAD_WIDTH' => {
            'kind' => 'top-parameter', 'name' => @payload_width_parameter
          },
          'DEPTH' => typed_parameter(depth, stage, 'fifo-depth'),
          'SYNC_STAGES' => typed_parameter(
            sync_stages, stage, 'metastability-stages'
          )
        },
        'sourceClock' => clock_reference(source),
        'destinationClock' => clock_reference(destination),
        'signals' => {
          'sourceBundle' => bridge.fetch('sourceSignal'),
          'destinationBundle' => bridge.fetch('destinationSignal')
        }
      }
    end

    def infrastructure_for(record, element)
      id = record.fetch('domain')
      planned = @context.entity_domain(
        element.fetch('kind'), element.fetch('id'), TIMING_ROLE
      ).fetch('domain')
      expect!(id == planned, 'rtl_evidence.bridge_domain_mismatch',
              '/domainRendering',
              "bridge Domain differs for #{element.fetch('kind')} #{element.fetch('id')}")
      infrastructure = @infrastructure_by_domain[id]
      expect!(infrastructure, 'rtl_evidence.unknown_clock_domain',
              '/domainRendering', "unknown timing Domain #{id}")
      expect!(record.fetch('clockSignal') == infrastructure.fetch('clockPort') &&
                record.fetch('resetSignal') == infrastructure.fetch('localResetSignal'),
              'rtl_evidence.bridge_signal_mismatch', '/domainRendering',
              "bridge clock/reset differs for Domain #{id}")
      infrastructure
    end

    def compile_deferred_items(realized_paths)
      relations = @context.plan.fetch('relationBindings').each_with_index.map do |relation, index|
        recipe = relation.fetch('recipe')
        {
          'kind' => 'relation', 'status' => 'deferred',
          'planPath' => "/relationBindings/#{index}",
          'relationType' => relation.fetch('relationType'),
          'role' => relation.fetch('role'),
          'domainType' => relation.fetch('domainType'),
          'fromDomain' => relation.fetch('fromDomain'),
          'toDomain' => relation.fetch('toDomain'),
          'recipes' => [recipe],
          'reasonCode' => recipe == 'derived-clock-divider' ?
            'renderer.derived_clock_not_materialized' :
            'renderer.relation_not_materialized'
        }
      end
      stages = @context.plan.fetch('edgeBindings').each_with_index.flat_map do |edge, edge_index|
        edge.fetch('stages').each_with_index.filter_map do |stage, stage_index|
          path = "/edgeBindings/#{edge_index}/stages/#{stage_index}"
          next if realized_paths[path]

          recipes = stage_recipes(stage)
          {
            'kind' => 'edge-stage', 'status' => 'deferred',
            'planPath' => path, 'edge' => copy(edge.fetch('edge')),
            'order' => stage.fetch('order'), 'role' => stage.fetch('role'),
            'domainType' => stage.fetch('domainType'), 'recipes' => recipes,
            'reasonCode' => (recipes - POWER_RECIPES).empty? ?
              'renderer.power_stage_not_materialized' :
              'renderer.stage_not_materialized'
          }
        end
      end
      relations.sort_by { |item| item.fetch('planPath') } +
        stages.sort_by { |item| [*edge_sort_key(item.fetch('edge')), item.fetch('order')] }
    end

    def typed_parameter(value, stage, name)
      parameter = stage.fetch('parameters').fetch(name)
      {'type' => 'integer', 'value' => value,
       'source' => copy(parameter.fetch('source'))}
    end

    def clock_reference(infrastructure)
      {
        'domain' => infrastructure.fetch('domain'),
        'clockSignal' => infrastructure.fetch('clockPort'),
        'resetSignal' => infrastructure.fetch('localResetSignal')
      }
    end

    def stage_recipes(stage)
      values = stage.key?('directions') ? stage.fetch('directions') : [stage]
      values.map { |entry| entry.fetch('recipe') }.uniq.sort
    end

    def edge_sort_key(reference)
      [EDGE_ORDER.fetch(reference.fetch('kind'), 99),
       reference.fetch('kind'), reference.fetch('id')]
    end

    def copy(value)
      case value
      when Hash then value.to_h { |key, child| [key, copy(child)] }
      when Array then value.map { |child| copy(child) }
      else value
      end
    end

    def canonicalize(value)
      case value
      when Hash
        value.keys.sort.to_h { |key| [key, canonicalize(value.fetch(key))] }
      when Array then value.map { |child| canonicalize(child) }
      else value
      end
    end

    def expect!(condition, code, path, message)
      fail!(code, path, message) unless condition
    end

    def fail!(code, path, message)
      raise DomainRtlEvidenceError.new(code, path, message)
    end
  end
end

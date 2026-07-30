# frozen_string_literal: true

require 'json'

module FinepaperNoc
  class DomainRealizationError < StandardError
    attr_reader :code, :path

    def initialize(code, path, message)
      @code = code
      @path = path
      super("#{message} at #{path}")
    end
  end

  # Compiles the normalized, Package-independent Domain constraints into a
  # typed implementation plan. Product ids are data: the compiler only
  # interprets realization roles, recipe kinds, selectors, and typed bindings
  # declared by domain-realization.json.
  class DomainRealizer
    VALUE_TYPES = %w[integer number boolean string enum].freeze
    RECIPE_KINDS = %w[
      bidirectional-stage directional-stage divide-source-binding unsupported
    ].freeze
    ELEMENT_KINDS = %w[router endpoint].freeze
    EDGE_KINDS = %w[router-link endpoint-attachment].freeze
    KIND_ORDER = {
      'router' => 0,
      'endpoint' => 1,
      'router-link' => 0,
      'endpoint-attachment' => 1
    }.freeze

    def self.load(path)
      new(JSON.parse(File.read(path)))
    rescue JSON::ParserError => error
      raise DomainRealizationError.new(
        'realization.invalid_json', '/', error.message
      )
    end

    def initialize(manifest)
      @manifest = object!(manifest, '/')
      parse_manifest!
    end

    def realize(constraints:, design:)
      constraints = object!(constraints, '/constraints')
      design = validate_design!(design)
      validate_input_headers!(constraints, design)

      graph = design_graph(design, constraints)
      domains = compile_domains(constraints, design, graph)
      memberships = compile_memberships(constraints, design, graph, domains)
      crossing_sources = compile_crossing_sources(constraints, design, graph, domains)
      relations = compile_relations(constraints, design, domains)
      crossings = compile_crossings(
        constraints, graph, domains, memberships, crossing_sources
      )

      domain_bindings = domains.values.map { |domain| domain.fetch(:output) }
                               .sort_by { |entry| [entry.fetch('role'), entry.fetch('domain')] }
      entity_bindings = graph.fetch(:entities).values.map do |element|
        assignments = memberships.fetch(reference_key(element), {})
        {
          'element' => element,
          'bindings' => assignments.flat_map do |domain_type, domain_ids|
            mapping = @domain_types.fetch(domain_type)
            domain_ids.map do |domain_id|
              {
                'role' => mapping.fetch('role'),
                'domainType' => domain_type,
                'domain' => domain_id
              }
            end
          end.sort_by { |entry| [entry.fetch('role'), entry.fetch('domain')] }
        }
      end.sort_by { |entry| reference_sort_key(entry.fetch('element')) }

      crossings_by_edge = crossings.group_by { |crossing| reference_key(crossing.fetch(:edge)) }
      edge_bindings = graph.fetch(:edges).values.map do |edge|
        from_assignments = memberships.fetch(reference_key(edge.fetch('fromElement')), {})
        to_assignments = memberships.fetch(reference_key(edge.fetch('toElement')), {})
        stages = crossings_by_edge.fetch(reference_key(edge.fetch('edge')), [])
                             .flat_map { |crossing| crossing.fetch(:stages) }
                             .sort_by do |stage|
          [stage.fetch('order'), stage.fetch('role'), stage.fetch('domainType')]
        end
        {
          'edge' => edge.fetch('edge'),
          'fromElement' => edge.fetch('fromElement'),
          'toElement' => edge.fetch('toElement'),
          'fromBindings' => assignment_bindings(from_assignments),
          'toBindings' => assignment_bindings(to_assignments),
          'stages' => stages
        }
      end.sort_by { |entry| reference_sort_key(entry.fetch('edge')) }

      canonical_json({
        'format' => @plan.fetch('format'),
        'formatVersion' => @plan.fetch('formatVersion'),
        'design' => design.fetch('id'),
        'source' => {
          'format' => constraints.fetch('format'),
          'formatVersion' => constraints.fetch('formatVersion')
        },
        'realization' => {
          'format' => @manifest.fetch('format'),
          'formatVersion' => @manifest.fetch('formatVersion')
        },
        'domainBindings' => domain_bindings,
        'relationBindings' => relations,
        'entityBindings' => entity_bindings,
        'edgeBindings' => edge_bindings
      })
    end

    def validate_design!(design)
      design = object!(design, '/design')
      normalized_design = @inputs.fetch('normalizedDesign')
      expect!(design['format'] == normalized_design.fetch('format'),
              'realization.invalid_design_format', '/design/format',
              'expected a normalized NoC Design')
      expect!(normalized_design.fetch('formatVersions').include?(design['formatVersion']),
              'realization.invalid_design_version', '/design/formatVersion',
              'unsupported normalized Design version')
      string!(design['id'], '/design/id')
      design
    end

    private

    def parse_manifest!
      exact_keys!(@manifest,
                  %w[format formatVersion inputs plan recipes domainTypes], '/')
      expect!(@manifest['format'] == 'finepaper.noc-domain-realization',
              'realization.unsupported_format', '/format',
              'unsupported realization manifest format')
      expect!(@manifest['formatVersion'] == 1,
              'realization.unsupported_version', '/formatVersion',
              'unsupported realization manifest version')

      inputs = object!(@manifest['inputs'], '/inputs')
      exact_keys!(inputs, %w[normalizedDesign compiledConstraints], '/inputs')
      normalized_design = object!(inputs['normalizedDesign'],
                                  '/inputs/normalizedDesign')
      exact_keys!(normalized_design, %w[format formatVersions],
                  '/inputs/normalizedDesign')
      string!(normalized_design['format'], '/inputs/normalizedDesign/format')
      format_versions = array!(normalized_design['formatVersions'],
                               '/inputs/normalizedDesign/formatVersions')
      expect!(!format_versions.empty?, 'realization.empty_input_versions',
              '/inputs/normalizedDesign/formatVersions',
              'normalized Design formatVersions must not be empty')
      format_versions.each_with_index do |version, index|
        integer!(version, "/inputs/normalizedDesign/formatVersions/#{index}")
      end
      expect!(format_versions.uniq.length == format_versions.length,
              'realization.duplicate_input_version',
              '/inputs/normalizedDesign/formatVersions',
              'normalized Design formatVersions must be unique')

      compiled_constraints = object!(inputs['compiledConstraints'],
                                     '/inputs/compiledConstraints')
      exact_keys!(compiled_constraints, %w[format formatVersion boundarySemantics],
                  '/inputs/compiledConstraints')
      string!(compiled_constraints['format'],
              '/inputs/compiledConstraints/format')
      integer!(compiled_constraints['formatVersion'],
               '/inputs/compiledConstraints/formatVersion')
      boundary_semantics = object!(compiled_constraints['boundarySemantics'],
                                   '/inputs/compiledConstraints/boundarySemantics')
      expect!(!boundary_semantics.empty?, 'realization.empty_boundary_semantics',
              '/inputs/compiledConstraints/boundarySemantics',
              'compiled constraints boundary semantics must not be empty')
      boundary_semantics.each do |key, value|
        string!(key, '/inputs/compiledConstraints/boundarySemantics')
        string!(value, "/inputs/compiledConstraints/boundarySemantics/#{key}")
      end
      @inputs = inputs

      @plan = object!(@manifest['plan'], '/plan')
      exact_keys!(@plan, %w[format formatVersion], '/plan')
      string!(@plan['format'], '/plan/format')
      integer!(@plan['formatVersion'], '/plan/formatVersion')

      raw_recipes = object!(@manifest['recipes'], '/recipes')
      expect!(!raw_recipes.empty?, 'realization.empty_recipes', '/recipes',
              'recipes must not be empty')
      @recipes = raw_recipes.each_with_object({}) do |(id, value), result|
        string!(id, '/recipes')
        recipe = object!(value, "/recipes/#{id}")
        allowed = recipe['kind'] == 'unsupported' ? %w[kind reason] : %w[kind]
        exact_keys!(recipe, allowed, "/recipes/#{id}")
        kind = string!(recipe['kind'], "/recipes/#{id}/kind")
        expect!(RECIPE_KINDS.include?(kind), 'realization.unknown_recipe_kind',
                "/recipes/#{id}/kind", "unknown recipe kind #{kind}")
        string!(recipe['reason'], "/recipes/#{id}/reason") if kind == 'unsupported'
        result[id] = recipe
      end

      raw_types = array!(@manifest['domainTypes'], '/domainTypes')
      expect!(!raw_types.empty?, 'realization.empty_domain_types', '/domainTypes',
              'domainTypes must not be empty')
      @domain_types = {}
      stage_orders = {}
      raw_types.each_with_index do |value, index|
        path = "/domainTypes/#{index}"
        mapping = object!(value, path)
        exact_keys!(mapping, %w[type role bindings relations crossingStages], path)
        type = string!(mapping['type'], "#{path}/type")
        string!(mapping['role'], "#{path}/role")
        expect!(!@domain_types.key?(type), 'realization.duplicate_domain_type',
                "#{path}/type", "duplicate Domain type mapping #{type}")
        parse_value_mappings!(array!(mapping['bindings'], "#{path}/bindings"),
                              "#{path}/bindings")
        bindings = mapping['bindings'].to_h { |entry| [entry.fetch('name'), entry] }
        parse_relations!(mapping, path, bindings)
        parse_crossing_stages!(mapping, path, bindings, stage_orders)
        @domain_types[type] = mapping
      end
    end

    def parse_relations!(mapping, path, bindings)
      relations = array!(mapping['relations'], "#{path}/relations")
      seen = {}
      relations.each_with_index do |value, index|
        relation_path = "#{path}/relations/#{index}"
        relation = object!(value, relation_path)
        exact_keys!(relation, %w[
          type role recipe sourceEndpoint targetEndpoint sourceBinding
          targetBinding ratioParameter parameters
        ], relation_path)
        type = string!(relation['type'], "#{relation_path}/type")
        expect!(!seen.key?(type), 'realization.duplicate_relation_mapping',
                "#{relation_path}/type", "duplicate relation mapping #{type}")
        seen[type] = true
        string!(relation['role'], "#{relation_path}/role")
        recipe = recipe!(relation['recipe'], "#{relation_path}/recipe")
        expect!(recipe['kind'] == 'divide-source-binding',
                'realization.invalid_relation_recipe', "#{relation_path}/recipe",
                'relation recipe must divide a source binding')
        %w[sourceEndpoint targetEndpoint].each do |key|
          endpoint = string!(relation[key], "#{relation_path}/#{key}")
          expect!(%w[from to].include?(endpoint), 'realization.invalid_endpoint',
                  "#{relation_path}/#{key}", 'relation endpoint must be from or to')
        end
        expect!(relation['sourceEndpoint'] != relation['targetEndpoint'],
                'realization.duplicate_relation_endpoint', relation_path,
                'source and target endpoints must differ')
        %w[sourceBinding targetBinding].each do |key|
          binding = string!(relation[key], "#{relation_path}/#{key}")
          expect!(bindings.key?(binding), 'realization.unknown_binding',
                  "#{relation_path}/#{key}", "unknown binding #{binding}")
          expect!(numeric_type?(bindings.fetch(binding).fetch('type')),
                  'realization.non_numeric_relation_binding',
                  "#{relation_path}/#{key}",
                  "relation binding #{binding} must be numeric")
        end
        parse_value_mappings!(array!(relation['parameters'], "#{relation_path}/parameters"),
                              "#{relation_path}/parameters")
        ratio = string!(relation['ratioParameter'], "#{relation_path}/ratioParameter")
        names = relation['parameters'].map { |entry| entry.fetch('name') }
        expect!(names.include?(ratio), 'realization.unknown_ratio_parameter',
                "#{relation_path}/ratioParameter", "unknown parameter #{ratio}")
        ratio_declaration = relation['parameters'].find do |entry|
          entry.fetch('name') == ratio
        end
        expect!(numeric_type?(ratio_declaration.fetch('type')),
                'realization.non_numeric_ratio_parameter',
                "#{relation_path}/ratioParameter",
                'relation ratio parameter must be numeric')
      end
    end

    def parse_crossing_stages!(mapping, path, bindings, stage_orders)
      stages = array!(mapping['crossingStages'], "#{path}/crossingStages")
      property_owners = {}
      stages.each_with_index do |value, index|
        stage_path = "#{path}/crossingStages/#{index}"
        stage = object!(value, stage_path)
        allowed = %w[order role recipe selector when parameters]
        exact_keys!(stage, allowed.select { |key| stage.key?(key) } | %w[order role parameters],
                    stage_path)
        order = integer!(stage['order'], "#{stage_path}/order")
        expect!(order >= 0, 'realization.invalid_stage_order', "#{stage_path}/order",
                'stage order must not be negative')
        expect!(!stage_orders.key?(order), 'realization.duplicate_stage_order',
                "#{stage_path}/order", "stage order #{order} is duplicated")
        stage_orders[order] = true
        string!(stage['role'], "#{stage_path}/role")
        expect!(stage.key?('recipe') ^ stage.key?('selector'),
                'realization.invalid_stage_recipe', stage_path,
                'stage must declare exactly one recipe or selector')
        if stage.key?('recipe')
          recipe = recipe!(stage['recipe'], "#{stage_path}/recipe")
          expect!(%w[bidirectional-stage unsupported].include?(recipe['kind']),
                  'realization.invalid_stage_recipe', "#{stage_path}/recipe",
                  'fixed stage recipe must be bidirectional or unsupported')
        else
          parse_selector!(stage['selector'], "#{stage_path}/selector", bindings)
        end
        parse_condition!(stage['when'], "#{stage_path}/when") if stage.key?('when')
        parse_value_mappings!(array!(stage['parameters'], "#{stage_path}/parameters"),
                              "#{stage_path}/parameters")
        declarations = stage.fetch('parameters').dup
        declarations << stage.fetch('when') if stage.key?('when')
        declarations << stage.fetch('selector') if stage.key?('selector')
        declarations.each do |declaration|
          property = declaration.fetch('property')
          expect!(!property_owners.key?(property),
                  'realization.duplicate_crossing_property',
                  "#{stage_path}/#{property}",
                  "crossing property #{property} is mapped more than once")
          property_owners[property] = stage_path
        end
      end
    end

    def parse_selector!(value, path, bindings)
      selector = object!(value, path)
      required = %w[property type recipes]
      optional = %w[automatic reverse directionParameter]
      exact_keys!(selector, required + optional.select { |key| selector.key?(key) }, path)
      string!(selector['property'], "#{path}/property")
      value_type!(selector['type'], "#{path}/type")
      recipes = object!(selector['recipes'], "#{path}/recipes")
      expect!(!recipes.empty?, 'realization.empty_selector', "#{path}/recipes",
              'selector recipes must not be empty')
      recipes.each do |selection, recipe_id|
        string!(selection, "#{path}/recipes")
        next if recipe_id.nil?

        recipe = recipe!(recipe_id, "#{path}/recipes/#{selection}")
        expected_kinds = if selector.key?('reverse')
                           %w[directional-stage unsupported]
                         else
                           %w[bidirectional-stage unsupported]
                         end
        expect!(expected_kinds.include?(recipe.fetch('kind')),
                'realization.invalid_selector_recipe',
                "#{path}/recipes/#{selection}",
                'selector recipe kind does not match its directional shape')
      end
      if selector.key?('automatic')
        automatic = object!(selector['automatic'], "#{path}/automatic")
        exact_keys!(automatic, %w[
          value resolver binding fromGreater fromLess equal
        ], "#{path}/automatic")
        string!(automatic['value'], "#{path}/automatic/value")
        resolver = string!(automatic['resolver'], "#{path}/automatic/resolver")
        expect!(resolver == 'compare-endpoint-binding',
                'realization.unknown_selector_resolver', "#{path}/automatic/resolver",
                "unknown selector resolver #{resolver}")
        binding = string!(automatic['binding'], "#{path}/automatic/binding")
        expect!(bindings.key?(binding), 'realization.unknown_binding',
                "#{path}/automatic/binding", "unknown binding #{binding}")
        expect!(numeric_type?(bindings.fetch(binding).fetch('type')),
                'realization.non_numeric_automatic_binding',
                "#{path}/automatic/binding",
                'automatic selector binding must be numeric')
        %w[fromGreater fromLess equal].each do |key|
          selection = string!(automatic[key], "#{path}/automatic/#{key}")
          expect!(recipes.key?(selection), 'realization.unknown_selection',
                  "#{path}/automatic/#{key}", "unknown selection #{selection}")
        end
      end
      expect!(selector.key?('reverse') == selector.key?('directionParameter'),
              'realization.invalid_directional_selector', path,
              'reverse and directionParameter must be declared together')
      return unless selector.key?('reverse')

      reverse = object!(selector['reverse'], "#{path}/reverse")
      exact_keys!(reverse, recipes.keys, "#{path}/reverse")
      recipes.each_key do |selection|
        expect!(reverse.key?(selection), 'realization.missing_reverse_selection',
                "#{path}/reverse", "missing reverse selection for #{selection}")
        target = string!(reverse[selection], "#{path}/reverse/#{selection}")
        expect!(recipes.key?(target), 'realization.unknown_selection',
                "#{path}/reverse/#{selection}", "unknown selection #{target}")
      end
      string!(selector['directionParameter'], "#{path}/directionParameter")
    end

    def parse_condition!(value, path)
      condition = object!(value, path)
      exact_keys!(condition, %w[property type equals], path)
      string!(condition['property'], "#{path}/property")
      type = value_type!(condition['type'], "#{path}/type")
      validate_value!(condition['equals'], type, condition, "#{path}/equals")
    end

    def parse_value_mappings!(values, path)
      seen_names = {}
      seen_properties = {}
      values.each_with_index do |value, index|
        entry_path = "#{path}/#{index}"
        entry = object!(value, entry_path)
        required = %w[name property type]
        optional = %w[default minimum maximum powerOfTwo]
        exact_keys!(entry, required + optional.select { |key| entry.key?(key) }, entry_path)
        name = string!(entry['name'], "#{entry_path}/name")
        property = string!(entry['property'], "#{entry_path}/property")
        type = value_type!(entry['type'], "#{entry_path}/type")
        expect!(!seen_names.key?(name), 'realization.duplicate_binding_name',
                "#{entry_path}/name", "duplicate binding name #{name}")
        expect!(!seen_properties.key?(property), 'realization.duplicate_property_mapping',
                "#{entry_path}/property", "duplicate property mapping #{property}")
        seen_names[name] = true
        seen_properties[property] = true
        bounds = %w[minimum maximum].select { |key| entry.key?(key) }
        expect!(bounds.empty? || numeric_type?(type),
                'realization.invalid_value_bounds', entry_path,
                'only numeric value mappings may declare bounds')
        bounds.each do |key|
          number!(entry[key], "#{entry_path}/#{key}")
        end
        if entry.key?('minimum') && entry.key?('maximum')
          expect!(entry['minimum'] <= entry['maximum'],
                  'realization.invalid_value_bounds', entry_path,
                  'minimum must not exceed maximum')
        end
        if entry.key?('powerOfTwo')
          expect!(entry['powerOfTwo'] == true && type == 'integer',
                  'realization.invalid_power_of_two_constraint', entry_path,
                  'powerOfTwo is valid only as true on an integer mapping')
        end
        validate_value!(entry['default'], type, entry,
                        "#{entry_path}/default") if entry.key?('default')
      end
    end

    def validate_input_headers!(constraints, design)
      compiled_constraints = @inputs.fetch('compiledConstraints')
      exact_keys!(constraints, %w[
        format formatVersion design boundarySemantics topology instances members
        relations policies overrides meshCrossings
      ], '/constraints')
      expect!(constraints['format'] == compiled_constraints.fetch('format'),
              'realization.invalid_constraints_format', '/constraints/format',
              'expected compiled Domain constraints')
      expect!(constraints['formatVersion'] == compiled_constraints.fetch('formatVersion'),
              'realization.invalid_constraints_version', '/constraints/formatVersion',
              'unsupported compiled Domain constraints version')
      expect!(canonical_json(constraints['boundarySemantics']) ==
                canonical_json(compiled_constraints.fetch('boundarySemantics')),
              'realization.invalid_boundary_semantics',
              '/constraints/boundarySemantics',
              'compiled Domain boundary semantics do not match this realizer')
      expect!(constraints['design'] == design['id'], 'realization.design_mismatch',
              '/constraints/design', 'constraints belong to another Design')
    end

    def design_graph(design, constraints)
      topology = object!(design['topology'], '/design/topology')
      expect!(topology['type'] == 'mesh', 'realization.unsupported_topology',
              '/design/topology/type', 'only normalized Mesh topology is supported')
      rows = integer!(topology['rows'], '/design/topology/rows')
      columns = integer!(topology['columns'], '/design/topology/columns')
      expect!(rows.positive? && columns.positive?, 'realization.invalid_topology',
              '/design/topology', 'Mesh dimensions must be positive')
      compiled_topology = object!(constraints['topology'], '/constraints/topology')
      exact_keys!(compiled_topology, %w[type rows columns], '/constraints/topology')
      expect!(compiled_topology == {
                'type' => 'mesh', 'rows' => rows, 'columns' => columns
              }, 'realization.topology_mismatch', '/constraints/topology',
              'compiled topology does not match the normalized Design')

      entities = {}
      edges = {}
      rows.times do |y|
        columns.times do |x|
          current = {'kind' => 'router', 'id' => "r-#{x}-#{y}"}
          entities[reference_key(current)] = current
          if x + 1 < columns
            east = {'kind' => 'router', 'id' => "r-#{x + 1}-#{y}"}
            add_edge!(edges, 'router-link', "link-#{current['id']}--#{east['id']}", current, east)
          end
          if y + 1 < rows
            south = {'kind' => 'router', 'id' => "r-#{x}-#{y + 1}"}
            add_edge!(edges, 'router-link', "link-#{current['id']}--#{south['id']}", current, south)
          end
        end
      end
      array!(design['endpoints'], '/design/endpoints').each_with_index do |value, index|
        endpoint = object!(value, "/design/endpoints/#{index}")
        id = string!(endpoint['id'], "/design/endpoints/#{index}/id")
        reference = {'kind' => 'endpoint', 'id' => id}
        expect!(!entities.key?(reference_key(reference)), 'realization.duplicate_entity',
                "/design/endpoints/#{index}/id", "duplicate entity #{id}")
        attachment = object!(endpoint['attachment'], "/design/endpoints/#{index}/attachment")
        router = object!(attachment['router'], "/design/endpoints/#{index}/attachment/router")
        x = integer!(router['x'], "/design/endpoints/#{index}/attachment/router/x")
        y = integer!(router['y'], "/design/endpoints/#{index}/attachment/router/y")
        router_reference = {'kind' => 'router', 'id' => "r-#{x}-#{y}"}
        expect!(entities.key?(reference_key(router_reference)),
                'realization.unknown_attachment_router',
                "/design/endpoints/#{index}/attachment/router",
                "Endpoint #{id} attaches outside the Mesh")
        entities[reference_key(reference)] = reference
        add_edge!(edges, 'endpoint-attachment', id, router_reference, reference)
      end
      {entities: entities, edges: edges}
    end

    def add_edge!(edges, kind, id, from, to)
      edge = {'kind' => kind, 'id' => id}
      edges[reference_key(edge)] = {
        'edge' => edge, 'fromElement' => from, 'toElement' => to
      }
    end

    def compile_domains(constraints, design, graph)
      design_domains = array!(design['domains'], '/design/domains').each_with_object({}) do |value, result|
        domain = object!(value, '/design/domains')
        id = string!(domain['id'], '/design/domains/id')
        expect!(!result.key?(id), 'realization.duplicate_domain', '/design/domains',
                "duplicate Domain #{id}")
        result[id] = domain
      end
      instances = array!(constraints['instances'], '/constraints/instances')
      domains = {}
      instances.each_with_index do |value, index|
        path = "/constraints/instances/#{index}"
        instance = object!(value, path)
        exact_keys!(instance, %w[id type name properties members], path)
        id = string!(instance['id'], "#{path}/id")
        type = string!(instance['type'], "#{path}/type")
        string!(instance['name'], "#{path}/name")
        mapping = @domain_types[type]
        expect!(mapping, 'realization.unmapped_domain_type', "#{path}/type",
                "Domain type #{type} has no realization mapping")
        expect!(!domains.key?(id), 'realization.duplicate_domain', "#{path}/id",
                "duplicate Domain #{id}")
        design_domain = design_domains[id]
        expect!(design_domain, 'realization.domain_not_in_design', "#{path}/id",
                "Domain #{id} is absent from the normalized Design")
        expect!(design_domain['type'] == type && design_domain['name'] == instance['name'] &&
                  canonical_json(design_domain['properties']) == canonical_json(instance['properties']),
                'realization.domain_mismatch', path,
                "compiled Domain #{id} differs from the normalized Design")
        properties = object!(instance['properties'], "#{path}/properties")
        validate_mapped_property_keys!(
          properties, mapping.fetch('bindings'), "#{path}/properties"
        )
        bindings = typed_values(properties, mapping.fetch('bindings'), "#{path}/properties",
                                'domain-property')
        members = array!(instance['members'], "#{path}/members").map.with_index do |member, member_index|
          reference = element_reference!(member, "#{path}/members/#{member_index}", ELEMENT_KINDS)
          expect!(graph.fetch(:entities).key?(reference_key(reference)),
                  'realization.unknown_entity', "#{path}/members/#{member_index}",
                  'Domain member is absent from the normalized Design')
          reference
        end.sort_by { |reference| reference_sort_key(reference) }
        domains[id] = {
          mapping: mapping,
          bindings: bindings,
          type: type,
          members: members,
          output: {
            'domain' => id,
            'domainType' => type,
            'role' => mapping.fetch('role'),
            'name' => instance.fetch('name'),
            'parameters' => bindings,
            'members' => members
          }
        }
      end
      expect!(domains.keys.sort == design_domains.keys.sort,
              'realization.domain_set_mismatch', '/constraints/instances',
              'compiled Domain set differs from the normalized Design')
      domains
    end

    def compile_memberships(constraints, design, graph, domains)
      compiled = membership_map(array!(constraints['members'], '/constraints/members'),
                                '/constraints/members', graph, domains)
      normalized = membership_map(array!(design['domainMemberships'], '/design/domainMemberships'),
                                  '/design/domainMemberships', graph, domains)
      expect!(canonical_json(compiled) == canonical_json(normalized),
              'realization.membership_mismatch', '/constraints/members',
              'compiled memberships differ from the normalized Design')
      domains.each do |domain_id, domain|
        actual = compiled.filter_map do |key, assignments|
          reference = graph.fetch(:entities).fetch(key)
          reference if assignments.fetch(domain.fetch(:type), []).include?(domain_id)
        end.sort_by { |reference| reference_sort_key(reference) }
        expect!(actual == domain.fetch(:members), 'realization.member_index_mismatch',
                '/constraints/instances', "member index for Domain #{domain_id} is inconsistent")
      end
      compiled
    end

    def compile_crossing_sources(constraints, design, graph, domains)
      compiled_policies = normalize_policies(
        array!(constraints['policies'], '/constraints/policies'),
        '/constraints/policies', domains
      )
      normalized_policies = normalize_policies(
        array!(design['crossingPolicies'], '/design/crossingPolicies'),
        '/design/crossingPolicies', domains
      )
      expect!(compiled_policies == normalized_policies,
              'realization.policy_mismatch', '/constraints/policies',
              'compiled crossing policies differ from the normalized Design')

      policies_by_id = compiled_policies.to_h do |policy|
        [policy.fetch('id'), policy]
      end
      compiled_overrides = normalize_overrides(
        array!(constraints['overrides'], '/constraints/overrides'),
        '/constraints/overrides', graph, policies_by_id, domains
      )
      normalized_overrides = normalize_overrides(
        array!(design['edgeOverrides'], '/design/edgeOverrides'),
        '/design/edgeOverrides', graph, policies_by_id, domains
      )
      expect!(compiled_overrides == normalized_overrides,
              'realization.override_mismatch', '/constraints/overrides',
              'compiled edge overrides differ from the normalized Design')

      {
        policies_by_id: policies_by_id,
        policies_by_pair: compiled_policies.to_h do |policy|
          [policy_pair_key(policy.fetch('domainType'), policy.fetch('from'),
                           policy.fetch('to')), policy]
        end,
        overrides_by_edge: compiled_overrides.to_h do |override|
          [edge_domain_key(override.fetch('edge'), override.fetch('domainType')),
           override]
        end
      }
    end

    def normalize_policies(values, base_path, domains)
      seen_ids = {}
      seen_pairs = {}
      values.map.with_index do |value, index|
        path = "#{base_path}/#{index}"
        policy = object!(value, path)
        exact_keys!(policy, %w[id domainType from to properties], path)
        id = string!(policy['id'], "#{path}/id")
        type = string!(policy['domainType'], "#{path}/domainType")
        from_id = string!(policy['from'], "#{path}/from")
        to_id = string!(policy['to'], "#{path}/to")
        expect!(!seen_ids.key?(id), 'realization.duplicate_policy', "#{path}/id",
                "duplicate crossing policy #{id}")
        from_domain = domains[from_id]
        to_domain = domains[to_id]
        expect!(from_domain && to_domain && from_domain.fetch(:type) == type &&
                  to_domain.fetch(:type) == type,
                'realization.invalid_policy_domains', path,
                'crossing policy Domains are unknown or have the wrong type')
        pair = policy_pair_key(type, from_id, to_id)
        expect!(!seen_pairs.key?(pair), 'realization.duplicate_policy_pair', path,
                'crossing policy Domain pair is duplicated')
        seen_ids[id] = true
        seen_pairs[pair] = true
        properties = canonical_json(object!(policy['properties'],
                                            "#{path}/properties"))
        validate_crossing_property_bag!(
          properties, from_domain.fetch(:mapping), "#{path}/properties",
          complete: true
        )
        {
          'id' => id,
          'domainType' => type,
          'from' => from_id,
          'to' => to_id,
          'properties' => properties
        }
      end.sort_by do |policy|
        [policy.fetch('domainType'), policy.fetch('from'), policy.fetch('to'),
         policy.fetch('id')]
      end
    end

    def normalize_overrides(values, base_path, graph, policies_by_id, domains)
      seen = {}
      values.map.with_index do |value, index|
        path = "#{base_path}/#{index}"
        override = object!(value, path)
        exact_keys!(override, %w[edge domainType policy properties], path)
        edge = edge_reference!(override['edge'], "#{path}/edge")
        expect!(graph.fetch(:edges).key?(reference_key(edge)),
                'realization.unknown_edge', "#{path}/edge",
                'edge override references an unknown edge')
        type = string!(override['domainType'], "#{path}/domainType")
        policy_id = string!(override['policy'], "#{path}/policy")
        policy = policies_by_id[policy_id]
        expect!(policy && policy.fetch('domainType') == type,
                'realization.invalid_override_policy', "#{path}/policy",
                'edge override policy is unknown or has the wrong Domain type')
        key = edge_domain_key(edge, type)
        expect!(!seen.key?(key), 'realization.duplicate_override', path,
                'edge has a duplicate Domain override')
        seen[key] = true
        properties = canonical_json(object!(override['properties'],
                                            "#{path}/properties"))
        domain = domains.fetch(policy.fetch('from'))
        validate_crossing_property_bag!(
          properties, domain.fetch(:mapping), "#{path}/properties",
          complete: false
        )
        {
          'edge' => edge,
          'domainType' => type,
          'policy' => policy_id,
          'properties' => properties
        }
      end.sort_by do |override|
        edge = override.fetch('edge')
        [edge.fetch('kind'), edge.fetch('id'), override.fetch('domainType')]
      end
    end

    def membership_map(values, base_path, graph, domains)
      values.each_with_index.each_with_object({}) do |(value, index), result|
        path = "#{base_path}/#{index}"
        membership = object!(value, path)
        exact_keys!(membership, %w[element assignments], path)
        element = element_reference!(membership['element'], "#{path}/element", ELEMENT_KINDS)
        key = reference_key(element)
        expect!(graph.fetch(:entities).key?(key), 'realization.unknown_entity',
                "#{path}/element", 'membership entity is absent from the normalized Design')
        expect!(!result.key?(key), 'realization.duplicate_membership', path,
                'entity has duplicate memberships')
        assignments = object!(membership['assignments'], "#{path}/assignments")
        result[key] = assignments.keys.sort.each_with_object({}) do |type, normalized|
          mapping = @domain_types[type]
          expect!(mapping, 'realization.unmapped_domain_type', "#{path}/assignments/#{type}",
                  "Domain type #{type} has no realization mapping")
          ids = array!(assignments[type], "#{path}/assignments/#{type}").map do |id|
            string!(id, "#{path}/assignments/#{type}")
          end.sort
          expect!(ids.uniq == ids, 'realization.duplicate_assignment',
                  "#{path}/assignments/#{type}", 'Domain assignment is duplicated')
          ids.each do |id|
            domain = domains[id]
            expect!(domain && domain.fetch(:type) == type,
                    'realization.invalid_assignment', "#{path}/assignments/#{type}",
                    "Domain assignment #{id} has the wrong type or is unknown")
          end
          normalized[type] = ids
        end
      end
    end

    def compile_relations(constraints, design, domains)
      compiled_values = array!(constraints['relations'], '/constraints/relations')
      normalized_values = array!(design['domainRelations'], '/design/domainRelations')
      expect!(canonical_relation_values(compiled_values, '/constraints/relations') ==
                canonical_relation_values(normalized_values, '/design/domainRelations'),
              'realization.relation_mismatch', '/constraints/relations',
              'compiled relations differ from the normalized Design')
      seen = {}
      compiled = compiled_values.map.with_index do |value, index|
        path = "/constraints/relations/#{index}"
        relation = object!(value, path)
        relation_type = string!(relation['type'], "#{path}/type")
        from_id = string!(relation['from'], "#{path}/from")
        to_id = string!(relation['to'], "#{path}/to")
        identity = [relation_type, from_id, to_id]
        expect!(!seen.key?(identity), 'realization.duplicate_relation', path,
                'Domain relation is duplicated')
        seen[identity] = true
        from = domains[from_id]
        to = domains[to_id]
        expect!(from && to, 'realization.unknown_relation_domain', path,
                'relation references an unknown Domain')
        expect!(from.fetch(:type) == to.fetch(:type),
                'realization.cross_type_relation', path,
                'relation endpoints must have the same Domain type')
        mapping = from.fetch(:mapping).fetch('relations').find do |entry|
          entry.fetch('type') == relation_type
        end
        expect!(mapping, 'realization.unmapped_relation_type', "#{path}/type",
                "relation type #{relation['type']} has no realization mapping")
        recipe = @recipes.fetch(mapping.fetch('recipe'))
        raise_unsupported_recipe!(mapping.fetch('recipe'), recipe, "#{path}/type")
        properties = object!(relation['properties'], "#{path}/properties")
        parameters = typed_values(properties, mapping.fetch('parameters'),
                                  "#{path}/properties", 'relation-property')
        expected = mapping.fetch('parameters').map { |entry| entry.fetch('property') }.sort
        expect!(properties.keys.sort == expected, 'realization.unconsumed_relation_property',
                "#{path}/properties", 'relation has missing or unmapped properties')
        source = mapping.fetch('sourceEndpoint') == 'from' ? from : to
        target = mapping.fetch('targetEndpoint') == 'from' ? from : to
        source_value = binding_value(source, mapping.fetch('sourceBinding'), path)
        target_value = binding_value(target, mapping.fetch('targetBinding'), path)
        ratio = parameters.fetch(mapping.fetch('ratioParameter')).fetch('value')
        expect!(ratio.is_a?(Numeric) && ratio.positive?, 'realization.invalid_ratio', path,
                'relation ratio must be positive')
        calculated = source_value.fetch('value').to_f / ratio
        expect!(numeric_equal?(calculated, target_value.fetch('value')),
                'realization.derived_binding_mismatch', path,
                'derived target binding does not match source divided by the declared ratio')
        {
          'relationType' => relation.fetch('type'),
          'domainType' => from.fetch(:type),
          'role' => mapping.fetch('role'),
          'recipe' => mapping.fetch('recipe'),
          'recipeKind' => recipe.fetch('kind'),
          'fromDomain' => relation.fetch('from'),
          'toDomain' => relation.fetch('to'),
          'sourceDomain' => source.fetch(:output).fetch('domain'),
          'targetDomain' => target.fetch(:output).fetch('domain'),
          'parameters' => parameters,
          'resolved' => {
            'sourceBindingName' => mapping.fetch('sourceBinding'),
            'targetBindingName' => mapping.fetch('targetBinding'),
            'sourceBinding' => source_value,
            'targetBinding' => target_value,
            'calculatedTarget' => {
              'type' => target_value.fetch('type'), 'value' => calculated
            }
          }
        }
      end
      validate_relation_dependencies!(compiled)
      compiled.sort_by do |entry|
        [entry.fetch('role'), entry.fetch('domainType'), entry.fetch('fromDomain'),
         entry.fetch('toDomain'), entry.fetch('relationType'), entry.fetch('recipe'),
         JSON.generate(canonical_json(entry.fetch('parameters')))]
      end
    end

    def validate_relation_dependencies!(relations)
      divide_relations = relations.select do |relation|
        relation.fetch('recipeKind') == 'divide-source-binding'
      end
      adjacency = Hash.new { |hash, key| hash[key] = [] }
      targets = {}
      divide_relations.each do |relation|
        source = [
          relation.fetch('domainType'), relation.fetch('sourceDomain'),
          relation.dig('resolved', 'sourceBindingName')
        ]
        target = [
          relation.fetch('domainType'), relation.fetch('targetDomain'),
          relation.dig('resolved', 'targetBindingName')
        ]
        expect!(source != target, 'realization.self_relation',
                '/constraints/relations',
                'divide-source binding must not depend on itself')
        expect!(!targets.key?(target), 'realization.multiple_relation_sources',
                '/constraints/relations',
                "binding #{target.join('/')} has multiple divide sources")
        targets[target] = source
        adjacency[source] << target
      end
      visiting = {}
      visited = {}
      visit = lambda do |binding|
        expect!(!visiting[binding], 'realization.relation_cycle',
                '/constraints/relations',
                'divide-source relations must form an acyclic graph')
        return if visited[binding]

        visiting[binding] = true
        adjacency[binding].sort.each { |target| visit.call(target) }
        visiting.delete(binding)
        visited[binding] = true
      end
      adjacency.keys.sort.each { |binding| visit.call(binding) }
    end

    def canonical_relation_values(values, base_path)
      values.map.with_index do |value, index|
        path = "#{base_path}/#{index}"
        relation = object!(value, path)
        exact_keys!(relation, %w[type from to properties], path)
        {
          'type' => relation['type'], 'from' => relation['from'], 'to' => relation['to'],
          'properties' => canonical_json(object!(relation['properties'],
                                                "#{path}/properties"))
        }
      end.sort_by { |entry| [entry['type'].to_s, entry['from'].to_s, entry['to'].to_s] }
    end

    def compile_crossings(constraints, graph, domains, memberships, sources)
      expected_crossings = expected_crossing_map(graph, memberships)
      unused_override = sources.fetch(:overrides_by_edge).keys.find do |key|
        !expected_crossings.key?(key)
      end
      expect!(!unused_override, 'realization.unused_override',
              '/constraints/overrides',
              'edge override does not target a Domain crossing')
      seen = {}
      crossings = array!(constraints['meshCrossings'], '/constraints/meshCrossings').map.with_index do |value, index|
        path = "/constraints/meshCrossings/#{index}"
        crossing = object!(value, path)
        exact_keys!(crossing, %w[
          edge fromElement toElement domainType fromDomains toDomains resolution
        ], path)
        edge = edge_reference!(crossing['edge'], "#{path}/edge")
        graph_edge = graph.fetch(:edges)[reference_key(edge)]
        expect!(graph_edge, 'realization.unknown_edge', "#{path}/edge",
                'crossing edge is absent from the normalized Design')
        from_element = element_reference!(crossing['fromElement'], "#{path}/fromElement", ELEMENT_KINDS)
        to_element = element_reference!(crossing['toElement'], "#{path}/toElement", ELEMENT_KINDS)
        expect!(from_element == graph_edge.fetch('fromElement') &&
                  to_element == graph_edge.fetch('toElement'),
                'realization.edge_orientation_mismatch', path,
                'crossing orientation differs from the normalized Design')
        type = string!(crossing['domainType'], "#{path}/domainType")
        mapping = @domain_types[type]
        expect!(mapping, 'realization.unmapped_domain_type', "#{path}/domainType",
                "Domain type #{type} has no realization mapping")
        key = edge_domain_key(edge, type)
        expect!(expected_crossings.key?(key), 'realization.unexpected_crossing', path,
                'compiled constraints contain a same-Domain or otherwise unexpected crossing')
        expect!(!seen.key?(key), 'realization.duplicate_crossing', path,
                'edge has a duplicate Domain crossing')
        seen[key] = true
        from_domain_id = singleton_domain!(crossing['fromDomains'], "#{path}/fromDomains")
        to_domain_id = singleton_domain!(crossing['toDomains'], "#{path}/toDomains")
        from_domain = domains[from_domain_id]
        to_domain = domains[to_domain_id]
        expect!(from_domain && to_domain && from_domain.fetch(:type) == type &&
                  to_domain.fetch(:type) == type,
                'realization.invalid_crossing_domain', path,
                'crossing Domains are unknown or have the wrong type')
        expect!(memberships.fetch(reference_key(from_element), {}).fetch(type, []) == [from_domain_id] &&
                  memberships.fetch(reference_key(to_element), {}).fetch(type, []) == [to_domain_id],
                'realization.crossing_membership_mismatch', path,
                'crossing Domains differ from entity bindings')
        expect!(from_domain_id != to_domain_id, 'realization.same_domain_crossing', path,
                'Domain crossing endpoints must use different Domains')
        resolution = object!(crossing['resolution'], "#{path}/resolution")
        exact_keys!(resolution, %w[
          source policy policyProperties overrideProperties effectiveProperties
        ], "#{path}/resolution")
        expected_resolution = crossing_resolution(
          sources, key, type, from_domain_id, to_domain_id, path
        )
        expect!(canonical_json(resolution) == expected_resolution,
                'realization.resolution_mismatch', "#{path}/resolution",
                'crossing resolution differs from its policy and edge override')
        properties = object!(resolution['effectiveProperties'],
                             "#{path}/resolution/effectiveProperties")
        validate_crossing_property_bag!(
          properties, mapping,
          "#{path}/resolution/effectiveProperties", complete: true
        )
        stages = mapping.fetch('crossingStages').filter_map do |stage|
          compile_stage(stage, properties, type, from_domain_id, to_domain_id,
                        from_domain, to_domain, resolution, path)
        end
        {edge: edge, type: type, stages: stages}
      end

      missing = expected_crossings.keys.find { |key| !seen.key?(key) }
      expect!(!missing, 'realization.missing_crossing', '/constraints/meshCrossings',
              'compiled constraints omit a Domain crossing')
      crossings
    end

    def expected_crossing_map(graph, memberships)
      graph.fetch(:edges).values.each_with_object({}) do |edge, result|
        from = memberships.fetch(reference_key(edge.fetch('fromElement')), {})
        to = memberships.fetch(reference_key(edge.fetch('toElement')), {})
        (from.keys | to.keys).sort.each do |type|
          from_domains = from.fetch(type, [])
          to_domains = to.fetch(type, [])
          next if from_domains == to_domains

          result[edge_domain_key(edge.fetch('edge'), type)] = {
            edge: edge.fetch('edge'), type: type,
            from_domains: from_domains, to_domains: to_domains
          }
        end
      end
    end

    def crossing_resolution(sources, key, type, from_id, to_id, path)
      override = sources.fetch(:overrides_by_edge)[key]
      policy = if override
                 sources.fetch(:policies_by_id).fetch(override.fetch('policy'))
               else
                 sources.fetch(:policies_by_pair)[policy_pair_key(type, from_id, to_id)]
               end
      expect!(policy, 'realization.missing_crossing_policy', path,
              'Domain crossing has no matching policy')
      expect!(policy.fetch('domainType') == type && policy.fetch('from') == from_id &&
                policy.fetch('to') == to_id,
              'realization.policy_orientation_mismatch', path,
              'crossing policy does not match the canonical edge orientation')
      override_properties = override ? override.fetch('properties') : {}
      canonical_json({
        'source' => override ? 'override' : 'policy',
        'policy' => policy.fetch('id'),
        'policyProperties' => policy.fetch('properties'),
        'overrideProperties' => override_properties,
        'effectiveProperties' => policy.fetch('properties').merge(override_properties)
      })
    end

    def compile_stage(stage, properties, type, from_id, to_id, from_domain, to_domain,
                      resolution, crossing_path)
      if stage.key?('when')
        condition = stage.fetch('when')
        value = crossing_property!(properties, condition, crossing_path)
        return nil unless value == condition.fetch('equals')
      end
      parameters = typed_values(properties, stage.fetch('parameters'),
                                "#{crossing_path}/resolution/effectiveProperties",
                                'crossing-property')
      base = {
        'order' => stage.fetch('order'),
        'role' => stage.fetch('role'),
        'domainType' => type,
        'fromDomain' => from_id,
        'toDomain' => to_id,
        'policy' => {
          'source' => resolution['source'],
          'id' => resolution['policy']
        },
        'parameters' => parameters
      }
      if stage.key?('recipe')
        recipe_id = stage.fetch('recipe')
        recipe = @recipes.fetch(recipe_id)
        raise_unsupported_recipe!(recipe_id, recipe, crossing_path)
        return base.merge('recipe' => recipe_id, 'recipeKind' => recipe.fetch('kind'))
      end

      selector = stage.fetch('selector')
      selection = crossing_property!(properties, selector, crossing_path)
      if selector.key?('automatic') && selection == selector.dig('automatic', 'value')
        selection = automatic_selection(selector.fetch('automatic'), from_domain, to_domain,
                                        crossing_path)
      end
      recipes = selector.fetch('recipes')
      expect!(recipes.key?(selection), 'realization.unknown_selection', crossing_path,
              "crossing selector value #{selection.inspect} is not mapped")
      unless selector.key?('reverse')
        recipe_id = recipes[selection]
        return nil if recipe_id.nil?

        recipe = @recipes.fetch(recipe_id)
        raise_unsupported_recipe!(recipe_id, recipe, crossing_path)
        return base.merge('recipe' => recipe_id, 'recipeKind' => recipe.fetch('kind'))
      end

      reverse_selection = selector.fetch('reverse').fetch(selection)
      directions = [
        ['from-to', selection], ['to-from', reverse_selection]
      ].filter_map do |orientation, directional_selection|
        recipe_id = recipes.fetch(directional_selection)
        next if recipe_id.nil?

        recipe = @recipes.fetch(recipe_id)
        raise_unsupported_recipe!(recipe_id, recipe, crossing_path)
        {
          'orientation' => orientation,
          'recipe' => recipe_id,
          'recipeKind' => recipe.fetch('kind'),
          'parameters' => {
            selector.fetch('directionParameter') => {
              'type' => selector.fetch('type'), 'value' => directional_selection,
              'source' => {'kind' => 'resolved-selector', 'id' => selector.fetch('property')}
            }
          }
        }
      end
      return nil if directions.empty?

      base.merge('directions' => directions)
    end

    def automatic_selection(automatic, from_domain, to_domain, path)
      from_value = binding_value(from_domain, automatic.fetch('binding'), path).fetch('value')
      to_value = binding_value(to_domain, automatic.fetch('binding'), path).fetch('value')
      expect!(from_value.is_a?(Numeric) && to_value.is_a?(Numeric),
              'realization.non_numeric_automatic_binding', path,
              'automatic selector requires numeric endpoint bindings')
      return automatic.fetch('fromGreater') if from_value > to_value
      return automatic.fetch('fromLess') if from_value < to_value

      automatic.fetch('equal')
    end

    def crossing_property_declarations(mapping)
      mapping.fetch('crossingStages').each_with_object({}) do |stage, result|
        declarations = stage.fetch('parameters').dup
        declarations << stage.fetch('when') if stage.key?('when')
        declarations << stage.fetch('selector') if stage.key?('selector')
        declarations.each do |declaration|
          result[declaration.fetch('property')] = declaration
        end
      end
    end

    def validate_crossing_property_bag!(properties, mapping, path, complete:)
      declarations = crossing_property_declarations(mapping)
      unknown = properties.keys - declarations.keys
      missing = declarations.reject do |property, declaration|
        properties.key?(property) || declaration.key?('default')
      end.keys
      expect!(unknown.empty?, 'realization.unmapped_crossing_property', path,
              "crossing property #{unknown.first} has no realization mapping")
      expect!(!complete || missing.empty?, 'realization.missing_crossing_property', path,
              "crossing property #{missing.first} is missing")
      properties.each do |property, value|
        declaration = declarations.fetch(property)
        validate_value!(value, declaration.fetch('type'), declaration,
                        "#{path}/#{property}")
        next unless declaration.key?('recipes')

        selections = declaration.fetch('recipes').keys
        selections << declaration.dig('automatic', 'value') if declaration.key?('automatic')
        expect!(selections.include?(value), 'realization.unknown_selection',
                "#{path}/#{property}",
                "crossing selector value #{value.inspect} is not mapped")
      end
    end

    def validate_mapped_property_keys!(properties, declarations, path)
      by_property = declarations.to_h do |declaration|
        [declaration.fetch('property'), declaration]
      end
      unknown = properties.keys - by_property.keys
      missing = by_property.reject do |property, declaration|
        properties.key?(property) || declaration.key?('default')
      end.keys
      expect!(unknown.empty?, 'realization.unconsumed_domain_property', path,
              "Domain property #{unknown.first} has no realization mapping")
      expect!(missing.empty?, 'realization.unconsumed_domain_property', path,
              "Domain property #{missing.first} is missing")
    end

    def crossing_property!(properties, declaration, path)
      property = declaration.fetch('property')
      value = properties.key?(property) ? properties[property] : declaration['default']
      expect!(!value.nil?, 'realization.missing_crossing_property', path,
              "crossing property #{property} is missing")
      validate_value!(value, declaration.fetch('type'), declaration,
                      "#{path}/#{property}")
      value
    end

    def typed_values(properties, declarations, path, source_kind)
      declarations.each_with_object({}) do |declaration, result|
        property = declaration.fetch('property')
        has_property = properties.key?(property)
        value = has_property ? properties[property] : declaration['default']
        expect!(!value.nil?, 'realization.missing_property', path,
                "property #{property} is missing")
        validate_value!(value, declaration.fetch('type'), declaration, "#{path}/#{property}")
        result[declaration.fetch('name')] = {
          'type' => declaration.fetch('type'),
          'value' => canonical_json(value),
          'source' => {
            'kind' => has_property ? source_kind : 'realization-default',
            'id' => property
          }
        }
      end
    end

    def validate_value!(value, type, declaration, path)
      valid = case type
              when 'integer' then value.is_a?(Integer)
              when 'number' then value.is_a?(Numeric) && (!value.respond_to?(:finite?) || value.finite?)
              when 'boolean' then value == true || value == false
              when 'string', 'enum' then value.is_a?(String) && !value.empty?
              else false
              end
      expect!(valid, 'realization.invalid_value_type', path,
              "expected #{type}, got #{value.class}")
      if declaration.key?('minimum')
        expect!(value >= declaration['minimum'], 'realization.value_below_minimum', path,
                "value is below #{declaration['minimum']}")
      end
      if declaration.key?('maximum')
        expect!(value <= declaration['maximum'], 'realization.value_above_maximum', path,
                "value is above #{declaration['maximum']}")
      end
      if declaration['powerOfTwo']
        expect!(value.positive? && (value & (value - 1)).zero?,
                'realization.value_not_power_of_two', path,
                'value must be a positive power of two')
      end
      value
    end

    def assignment_bindings(assignments)
      assignments.flat_map do |type, domain_ids|
        role = @domain_types.fetch(type).fetch('role')
        domain_ids.map { |id| {'role' => role, 'domainType' => type, 'domain' => id} }
      end.sort_by { |entry| [entry.fetch('role'), entry.fetch('domain')] }
    end

    def binding_value(domain, name, path)
      value = domain.fetch(:bindings)[name]
      expect!(value, 'realization.missing_binding', path, "binding #{name} is missing")
      value
    end

    def recipe!(id, path)
      id = string!(id, path)
      recipe = @recipes[id]
      expect!(recipe, 'realization.unknown_recipe', path, "unknown recipe #{id}")
      recipe
    end

    def raise_unsupported_recipe!(id, recipe, path)
      return unless recipe.fetch('kind') == 'unsupported'

      fail!('realization.unsupported_recipe', path,
            "recipe #{id} is unsupported: #{recipe.fetch('reason')}")
    end

    def singleton_domain!(value, path)
      values = array!(value, path)
      expect!(values.size == 1, 'realization.non_singleton_crossing', path,
              'realization requires exactly one Domain on each crossing side')
      string!(values.first, path)
    end

    def element_reference!(value, path, allowed)
      reference = object!(value, path)
      exact_keys!(reference, %w[kind id], path)
      kind = string!(reference['kind'], "#{path}/kind")
      expect!(allowed.include?(kind), 'realization.invalid_element_kind', "#{path}/kind",
              "invalid element kind #{kind}")
      {'kind' => kind, 'id' => string!(reference['id'], "#{path}/id")}
    end

    def edge_reference!(value, path)
      element_reference!(value, path, EDGE_KINDS)
    end

    def reference_key(reference)
      [reference.fetch('kind'), reference.fetch('id')]
    end

    def edge_domain_key(edge, type)
      [edge.fetch('kind'), edge.fetch('id'), type]
    end

    def policy_pair_key(type, from, to)
      [type, from, to]
    end

    def reference_sort_key(reference)
      [KIND_ORDER.fetch(reference.fetch('kind'), 99), reference.fetch('kind'), reference.fetch('id')]
    end

    def exact_keys!(object, allowed, path)
      unknown = object.keys - allowed
      missing = allowed - object.keys
      fail!('realization.unknown_field', "#{path}/#{unknown.first}",
            "unknown field #{unknown.first}") unless unknown.empty?
      fail!('realization.missing_field', "#{path}/#{missing.first}",
            "missing field #{missing.first}") unless missing.empty?
    end

    def object!(value, path)
      expect!(value.is_a?(Hash), 'realization.expected_object', path, 'expected an object')
      value
    end

    def array!(value, path)
      expect!(value.is_a?(Array), 'realization.expected_array', path, 'expected an array')
      value
    end

    def string!(value, path)
      expect!(value.is_a?(String) && !value.strip.empty?,
              'realization.expected_string', path, 'expected a non-empty string')
      value
    end

    def integer!(value, path)
      expect!(value.is_a?(Integer), 'realization.expected_integer', path,
              'expected an integer')
      value
    end

    def number!(value, path)
      finite = !value.respond_to?(:finite?) || value.finite?
      expect!(value.is_a?(Numeric) && finite, 'realization.expected_number', path,
              'expected a number')
      value
    end

    def numeric_type?(type)
      %w[integer number].include?(type)
    end

    def value_type!(value, path)
      type = string!(value, path)
      expect!(VALUE_TYPES.include?(type), 'realization.unknown_value_type', path,
              "unknown value type #{type}")
      type
    end

    def numeric_equal?(left, right)
      return false unless right.is_a?(Numeric)

      (left.to_f - right.to_f).abs <= 1e-9 * [left.to_f.abs, right.to_f.abs, 1.0].max
    end

    def canonical_json(value)
      case value
      when Hash
        value.keys.sort.each_with_object({}) do |key, result|
          result[key] = canonical_json(value.fetch(key))
        end
      when Array
        value.map { |entry| canonical_json(entry) }
      else value
      end
    end

    def expect!(condition, code, path, message)
      fail!(code, path, message) unless condition
      condition
    end

    def fail!(code, path, message)
      raise DomainRealizationError.new(code, path, message)
    end
  end
end

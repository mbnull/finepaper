# frozen_string_literal: true

require 'digest'

module FinepaperNoc
  class DomainRtlContextError < StandardError
    attr_reader :code, :path

    def initialize(code, path, message)
      @code = code
      @path = path
      super("#{message} at #{path}")
    end
  end

  # Strict, renderer-facing view of a typed Domain implementation plan.
  # Product ids stay in the plan; this class validates references, creates
  # collision-safe HDL tokens, and exposes physical-edge traffic orientation.
  class DomainRtlContext
    PLAN_FORMAT = 'finepaper.noc-domain-implementation-plan'
    PLAN_VERSION = 1
    SOURCE_FORMAT = 'finepaper.noc-domain-constraints'
    SOURCE_VERSION = 1
    REALIZATION_FORMAT = 'finepaper.noc-domain-realization'
    REALIZATION_VERSION = 1
    ORIENTATIONS = %w[from-to to-from].freeze
    EDGE_RECIPE_KINDS = %w[bidirectional-stage directional-stage].freeze
    RELATION_RECIPE_KINDS = %w[divide-source-binding].freeze
    VALUE_TYPES = %w[integer number boolean string enum].freeze

    attr_reader :plan, :domains, :relations, :entities, :edges

    def initialize(plan)
      @plan = object!(plan, '/')
      exact_keys!(@plan, %w[
        format formatVersion design source realization domainBindings
        relationBindings entityBindings edgeBindings
      ], '/')
      expect!(@plan['format'] == PLAN_FORMAT, 'rtl_context.invalid_format',
              '/format', 'unsupported Domain implementation plan format')
      expect!(@plan['formatVersion'] == PLAN_VERSION,
              'rtl_context.invalid_version', '/formatVersion',
              'unsupported Domain implementation plan version')
      string!(@plan['design'], '/design')
      validate_header!(@plan['source'], '/source', SOURCE_FORMAT, SOURCE_VERSION)
      validate_header!(@plan['realization'], '/realization',
                       REALIZATION_FORMAT, REALIZATION_VERSION)

      @domains = compile_domains(array!(@plan['domainBindings'], '/domainBindings'))
      @relations = compile_relations(
        array!(@plan['relationBindings'], '/relationBindings')
      )
      @entities = compile_entities(array!(@plan['entityBindings'], '/entityBindings'))
      @edges = compile_edges(array!(@plan['edgeBindings'], '/edgeBindings'))
      validate_entity_members!
      validate_edge_bindings!
      deep_freeze!(@plan)
      deep_freeze!(@domains)
      deep_freeze!(@relations)
      deep_freeze!(@entities)
      deep_freeze!(@edges)
    end

    def domains_for_role(role)
      role = role.to_s
      @domains.values.select { |domain| domain.fetch('role') == role }
              .sort_by { |domain| domain.fetch('domain') }
    end

    def entity(kind, id)
      @entities[reference_key(kind, id)]
    end

    def entity_domain(kind, id, role)
      entry = entity(kind, id)
      fail!('rtl_context.unknown_entity', '/entityBindings',
            "unknown entity #{kind} #{id}") unless entry
      matches = entry.fetch('bindings').select do |binding|
        binding.fetch('role') == role
      end
      expect!(matches.size == 1, 'rtl_context.non_singleton_role_binding',
              '/entityBindings',
              "entity #{kind} #{id} requires exactly one #{role} binding")
      @domains.fetch(matches.first.fetch('domain'))
    end

    def edge(kind, id)
      @edges[reference_key(kind, id)]
    end

    def edge_stage(kind, id, recipe)
      entry = edge(kind, id)
      fail!('rtl_context.unknown_edge', '/edgeBindings',
            "unknown edge #{kind} #{id}") unless entry
      matches = entry.fetch('stages').select do |stage|
        stage['recipe'] == recipe ||
          stage.fetch('directions', []).any? { |direction| direction['recipe'] == recipe }
      end
      expect!(matches.size <= 1, 'rtl_context.duplicate_recipe_stage',
              '/edgeBindings', "edge #{id} repeats recipe #{recipe}")
      matches.first
    end

    def traffic(kind, id, orientation)
      expect!(ORIENTATIONS.include?(orientation),
              'rtl_context.invalid_orientation', '/orientation',
              "unknown traffic orientation #{orientation}")
      entry = edge(kind, id)
      fail!('rtl_context.unknown_edge', '/edgeBindings',
            "unknown edge #{kind} #{id}") unless entry
      forward = orientation == 'from-to'
      {
        'orientation' => orientation,
        'producer' => forward ? entry.fetch('fromElement') : entry.fetch('toElement'),
        'consumer' => forward ? entry.fetch('toElement') : entry.fetch('fromElement'),
        'stages' => entry.fetch('stages').map do |stage|
          directional_stage(stage, orientation)
        end.compact
      }
    end

    def parameter_value(stage, name, expected_type: nil)
      parameters = object!(stage.fetch('parameters', {}), '/stage/parameters')
      parameter = parameters[name]
      fail!('rtl_context.missing_parameter', '/stage/parameters',
            "stage parameter #{name} is missing") unless parameter
      type = parameter.fetch('type')
      expect!(!expected_type || type == expected_type,
              'rtl_context.parameter_type_mismatch', '/stage/parameters',
              "stage parameter #{name} must be #{expected_type}")
      parameter.fetch('value')
    end

    private

    def compile_domains(values)
      token_registry = {}
      values.each_with_index.each_with_object({}) do |(value, index), result|
        path = "/domainBindings/#{index}"
        domain = object!(value, path)
        exact_keys!(domain, %w[
          domain domainType role name parameters members
        ], path)
        id = string!(domain['domain'], "#{path}/domain")
        expect!(!result.key?(id), 'rtl_context.duplicate_domain',
                "#{path}/domain", "duplicate Domain #{id}")
        token = hdl_token(id)
        expect!(!token_registry.key?(token), 'rtl_context.token_collision',
                "#{path}/domain", "HDL token collision with #{token_registry[token]}")
        token_registry[token] = id
        string!(domain['domainType'], "#{path}/domainType")
        string!(domain['role'], "#{path}/role")
        string!(domain['name'], "#{path}/name")
        validate_parameters!(object!(domain['parameters'], "#{path}/parameters"),
                             "#{path}/parameters")
        members = array!(domain['members'], "#{path}/members")
        members.each_with_index do |member, member_index|
          reference!(member, "#{path}/members/#{member_index}")
        end
        result[id] = domain.merge('token' => token)
      end
    end

    def compile_relations(values)
      values.each_with_index.each_with_object({}) do |(value, index), result|
        path = "/relationBindings/#{index}"
        relation = object!(value, path)
        exact_keys!(relation, %w[
          relationType domainType role recipe recipeKind fromDomain toDomain
          sourceDomain targetDomain parameters resolved
        ], path)
        relation_type = string!(relation['relationType'],
                                "#{path}/relationType")
        domain_type = string!(relation['domainType'], "#{path}/domainType")
        %w[role recipe fromDomain toDomain sourceDomain targetDomain].each do |key|
          string!(relation[key], "#{path}/#{key}")
        end
        kind = string!(relation['recipeKind'], "#{path}/recipeKind")
        expect!(RELATION_RECIPE_KINDS.include?(kind),
                'rtl_context.invalid_recipe_kind', "#{path}/recipeKind",
                'unsupported relation recipe kind')
        relation_domains = %w[
          fromDomain toDomain sourceDomain targetDomain
        ].map { |key| @domains[relation.fetch(key)] }
        domains_match = relation_domains.all? && relation_domains.all? do |domain|
          domain.fetch('domainType') == domain_type
        end
        expect!(domains_match,
                'rtl_context.relation_domain_mismatch', path,
                'relation Domains are unknown or have the wrong type')
        validate_parameters!(relation['parameters'], "#{path}/parameters")
        validate_relation_resolution!(relation['resolved'],
                                      "#{path}/resolved")
        identity = [relation_type, relation.fetch('fromDomain'),
                    relation.fetch('toDomain')]
        expect!(!result.key?(identity), 'rtl_context.duplicate_relation', path,
                'relation binding is duplicated')
        result[identity] = relation
      end
    end

    def validate_relation_resolution!(value, path)
      resolution = object!(value, path)
      exact_keys!(resolution, %w[
        sourceBindingName targetBindingName sourceBinding targetBinding
        calculatedTarget
      ], path)
      string!(resolution['sourceBindingName'], "#{path}/sourceBindingName")
      string!(resolution['targetBindingName'], "#{path}/targetBindingName")
      validate_parameter!(resolution['sourceBinding'], "#{path}/sourceBinding")
      validate_parameter!(resolution['targetBinding'], "#{path}/targetBinding")
      calculated = object!(resolution['calculatedTarget'],
                           "#{path}/calculatedTarget")
      exact_keys!(calculated, %w[type value], "#{path}/calculatedTarget")
      validate_typed_value!(calculated['value'], calculated['type'],
                            "#{path}/calculatedTarget")
    end

    def compile_entities(values)
      values.each_with_index.each_with_object({}) do |(value, index), result|
        path = "/entityBindings/#{index}"
        entry = object!(value, path)
        exact_keys!(entry, %w[element bindings], path)
        element = reference!(entry['element'], "#{path}/element")
        key = reference_key(element.fetch('kind'), element.fetch('id'))
        expect!(!result.key?(key), 'rtl_context.duplicate_entity', path,
                'entity binding is duplicated')
        bindings = validate_bindings!(entry['bindings'], "#{path}/bindings")
        result[key] = {'element' => element, 'bindings' => bindings}
      end
    end

    def compile_edges(values)
      values.each_with_index.each_with_object({}) do |(value, index), result|
        path = "/edgeBindings/#{index}"
        entry = object!(value, path)
        exact_keys!(entry, %w[
          edge fromElement toElement fromBindings toBindings stages
        ], path)
        edge = reference!(entry['edge'], "#{path}/edge")
        key = reference_key(edge.fetch('kind'), edge.fetch('id'))
        expect!(!result.key?(key), 'rtl_context.duplicate_edge', path,
                'edge binding is duplicated')
        from_element = reference!(entry['fromElement'], "#{path}/fromElement")
        to_element = reference!(entry['toElement'], "#{path}/toElement")
        from_bindings = validate_bindings!(entry['fromBindings'],
                                           "#{path}/fromBindings")
        to_bindings = validate_bindings!(entry['toBindings'],
                                         "#{path}/toBindings")
        stages = validate_stages!(entry['stages'], "#{path}/stages")
        result[key] = {
          'edge' => edge,
          'fromElement' => from_element,
          'toElement' => to_element,
          'fromBindings' => from_bindings,
          'toBindings' => to_bindings,
          'stages' => stages
        }
      end
    end

    def validate_bindings!(value, path)
      seen_roles = {}
      array!(value, path).map.with_index do |binding_value, index|
        binding_path = "#{path}/#{index}"
        binding = object!(binding_value, binding_path)
        exact_keys!(binding, %w[role domainType domain], binding_path)
        role = string!(binding['role'], "#{binding_path}/role")
        domain_id = string!(binding['domain'], "#{binding_path}/domain")
        domain = @domains[domain_id]
        expect!(domain, 'rtl_context.unknown_domain', "#{binding_path}/domain",
                "unknown Domain #{domain_id}")
        expect!(domain.fetch('role') == role &&
                  domain.fetch('domainType') == binding['domainType'],
                'rtl_context.binding_mismatch', binding_path,
                'binding role or Domain type does not match its Domain')
        key = [role, domain_id]
        expect!(!seen_roles.key?(key), 'rtl_context.duplicate_binding', binding_path,
                'binding is duplicated')
        seen_roles[key] = true
        binding
      end
    end

    def validate_stages!(value, path)
      previous_order = -1
      array!(value, path).map.with_index do |stage_value, index|
        stage_path = "#{path}/#{index}"
        stage = object!(stage_value, stage_path)
        common = %w[
          order role domainType fromDomain toDomain policy parameters
        ]
        directional = stage.key?('directions')
        exact_keys!(stage,
                    common + (directional ? %w[directions] : %w[recipe recipeKind]),
                    stage_path)
        order = integer!(stage['order'], "#{stage_path}/order")
        expect!(order > previous_order, 'rtl_context.unordered_stages',
                "#{stage_path}/order", 'edge stages must be strictly ordered')
        previous_order = order
        %w[role domainType fromDomain toDomain].each do |key|
          string!(stage[key], "#{stage_path}/#{key}")
        end
        from_domain = @domains[stage['fromDomain']]
        to_domain = @domains[stage['toDomain']]
        expect!(from_domain && to_domain,
                'rtl_context.unknown_stage_domain', stage_path,
                'stage references an unknown Domain')
        expect!(from_domain.fetch('domainType') == stage['domainType'] &&
                  to_domain.fetch('domainType') == stage['domainType'],
                'rtl_context.stage_domain_mismatch', stage_path,
                'stage Domain type differs from its endpoint Domains')
        validate_policy!(stage['policy'], "#{stage_path}/policy")
        validate_parameters!(stage['parameters'], "#{stage_path}/parameters")
        if directional
          directions = array!(stage['directions'], "#{stage_path}/directions")
          expect!(directions.size == 2, 'rtl_context.incomplete_directions',
                  "#{stage_path}/directions",
                  'directional stage must describe both traffic orientations')
          directions.each_with_index do |direction, direction_index|
            validate_direction!(direction,
                                "#{stage_path}/directions/#{direction_index}")
          end
          orientations = directions.map { |direction| direction.fetch('orientation') }
          expect!(orientations.sort == ORIENTATIONS.sort,
                  'rtl_context.incomplete_directions',
                  "#{stage_path}/directions",
                  'directional stage must uniquely cover from-to and to-from')
        else
          validate_recipe!(stage, stage_path, 'bidirectional-stage')
        end
        stage
      end
    end

    def validate_direction!(value, path)
      direction = object!(value, path)
      exact_keys!(direction, %w[orientation recipe recipeKind parameters], path)
      orientation = string!(direction['orientation'], "#{path}/orientation")
      expect!(ORIENTATIONS.include?(orientation), 'rtl_context.invalid_orientation',
              "#{path}/orientation", "unknown traffic orientation #{orientation}")
      validate_recipe!(direction, path, 'directional-stage')
      validate_parameters!(direction['parameters'], "#{path}/parameters")
    end

    def validate_recipe!(entry, path, expected_kind)
      string!(entry['recipe'], "#{path}/recipe")
      kind = string!(entry['recipeKind'], "#{path}/recipeKind")
      expect!(EDGE_RECIPE_KINDS.include?(kind) && kind == expected_kind,
              'rtl_context.invalid_recipe_kind', "#{path}/recipeKind",
              "recipe kind must be #{expected_kind}")
    end

    def validate_policy!(value, path)
      policy = object!(value, path)
      exact_keys!(policy, %w[source id], path)
      string!(policy['source'], "#{path}/source")
      string!(policy['id'], "#{path}/id")
    end

    def validate_parameters!(value, path)
      object!(value, path).each do |name, parameter_value|
        string!(name, path)
        validate_parameter!(parameter_value, "#{path}/#{name}")
      end
    end

    def validate_parameter!(value, path)
      parameter = object!(value, path)
      exact_keys!(parameter, %w[type value source], path)
      validate_typed_value!(parameter['value'], parameter['type'], path)
      source = object!(parameter['source'], "#{path}/source")
      exact_keys!(source, %w[kind id], "#{path}/source")
      string!(source['kind'], "#{path}/source/kind")
      string!(source['id'], "#{path}/source/id")
      parameter
    end

    def validate_typed_value!(value, type, path)
      type = string!(type, "#{path}/type")
      expect!(VALUE_TYPES.include?(type), 'rtl_context.unknown_value_type',
              "#{path}/type", "unknown value type #{type}")
      valid = case type
              when 'integer' then value.is_a?(Integer)
              when 'number'
                value.is_a?(Numeric) &&
                  (!value.respond_to?(:finite?) || value.finite?)
              when 'boolean' then value == true || value == false
              when 'string', 'enum'
                value.is_a?(String) && !value.empty?
              end
      expect!(valid, 'rtl_context.invalid_parameter_value', "#{path}/value",
              "value does not match declared type #{type}")
      value
    end

    def validate_entity_members!
      @domains.each_value do |domain|
        expected = domain.fetch('members').map do |member|
          reference_key(member.fetch('kind'), member.fetch('id'))
        end.sort
        actual = @entities.values.filter_map do |entity_entry|
          binding = entity_entry.fetch('bindings').find do |candidate|
            candidate.fetch('domain') == domain.fetch('domain')
          end
          element = entity_entry.fetch('element')
          reference_key(element.fetch('kind'), element.fetch('id')) if binding
        end.sort
        expect!(actual == expected, 'rtl_context.member_mismatch',
                '/domainBindings',
                "Domain #{domain.fetch('domain')} member index is inconsistent")
      end
    end

    def validate_edge_bindings!
      @edges.each_value do |edge_entry|
        from = edge_entry.fetch('fromElement')
        to = edge_entry.fetch('toElement')
        from_entity = entity(from.fetch('kind'), from.fetch('id'))
        to_entity = entity(to.fetch('kind'), to.fetch('id'))
        expect!(from_entity && to_entity, 'rtl_context.unknown_edge_entity',
                '/edgeBindings', 'edge endpoint is absent from entity bindings')
        expect!(edge_entry.fetch('fromBindings') == from_entity.fetch('bindings') &&
                  edge_entry.fetch('toBindings') == to_entity.fetch('bindings'),
                'rtl_context.edge_binding_mismatch', '/edgeBindings',
                'edge endpoint bindings differ from entity bindings')
      end
    end

    def directional_stage(stage, orientation)
      return stage unless stage.key?('directions')

      direction = stage.fetch('directions').find do |entry|
        entry.fetch('orientation') == orientation
      end
      return nil unless direction

      stage.reject { |key, _| key == 'directions' }.merge(
        'orientation' => orientation,
        'recipe' => direction.fetch('recipe'),
        'recipeKind' => direction.fetch('recipeKind'),
        'directionParameters' => direction.fetch('parameters')
      )
    end

    def hdl_token(id)
      utf8 = id.encode(Encoding::UTF_8, invalid: :replace, undef: :replace,
                       replace: "\uFFFD")
      readable = utf8.gsub(/[^A-Za-z0-9]+/, '_').gsub(/\A_+|_+\z/, '')
      readable = 'domain' if readable.empty?
      readable = "d_#{readable}" unless readable.match?(/\A[A-Za-z_]/)
      readable = readable[0, 32]
      "#{readable}_#{Digest::SHA256.hexdigest(utf8)}"
    end

    def validate_header!(value, path, expected_format, expected_version)
      header = object!(value, path)
      exact_keys!(header, %w[format formatVersion], path)
      expect!(header['format'] == expected_format,
              'rtl_context.invalid_header_format', "#{path}/format",
              "expected #{expected_format}")
      expect!(header['formatVersion'] == expected_version,
              'rtl_context.invalid_header_version', "#{path}/formatVersion",
              "expected format version #{expected_version}")
    end

    def reference!(value, path)
      reference = object!(value, path)
      exact_keys!(reference, %w[kind id], path)
      {
        'kind' => string!(reference['kind'], "#{path}/kind"),
        'id' => string!(reference['id'], "#{path}/id")
      }
    end

    def reference_key(kind, id)
      [kind.to_s, id.to_s]
    end

    def exact_keys!(object, allowed, path)
      unknown = object.keys - allowed
      missing = allowed - object.keys
      fail!('rtl_context.unknown_field', "#{path}/#{unknown.first}",
            "unknown field #{unknown.first}") unless unknown.empty?
      fail!('rtl_context.missing_field', "#{path}/#{missing.first}",
            "missing field #{missing.first}") unless missing.empty?
    end

    def object!(value, path)
      expect!(value.is_a?(Hash), 'rtl_context.expected_object', path,
              'expected an object')
      value
    end

    def array!(value, path)
      expect!(value.is_a?(Array), 'rtl_context.expected_array', path,
              'expected an array')
      value
    end

    def string!(value, path)
      expect!(value.is_a?(String) && !value.strip.empty?,
              'rtl_context.expected_string', path,
              'expected a non-empty string')
      value
    end

    def integer!(value, path)
      expect!(value.is_a?(Integer), 'rtl_context.expected_integer', path,
              'expected an integer')
      value
    end

    def deep_freeze!(value)
      case value
      when Hash
        value.each { |key, child| deep_freeze!(key); deep_freeze!(child) }
      when Array
        value.each { |child| deep_freeze!(child) }
      end
      value.freeze
    end

    def expect!(condition, code, path, message)
      fail!(code, path, message) unless condition
      condition
    end

    def fail!(code, path, message)
      raise DomainRtlContextError.new(code, path, message)
    end
  end
end

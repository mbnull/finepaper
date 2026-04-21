require 'set'
require_relative '../model/module_catalog'

class ValidRouterConnections < DrcBase
  def check(noc)
    xp_by_id = noc.xps.each_with_object({}) { |xp, result| result[xp.id] = xp }
    xp_ids = xp_by_id.keys.to_set

    noc.connections.flat_map do |connection|
      errors = []
      from_xp = xp_by_id[connection.from]
      to_xp = xp_by_id[connection.to]

      unless xp_ids.include?(connection.from)
        errors << "Invalid connection source: #{connection.from} not found"
      end

      unless xp_ids.include?(connection.to)
        errors << "Invalid connection target: #{connection.to} not found"
      end

      if from_xp && to_xp
        errors << "XP #{connection.from}: connection cannot target itself" if connection.from == connection.to

        direction_errors = validate_router_direction(connection)
        errors.concat(direction_errors)

        if direction_errors.empty?
          valid_directions = from_xp.module_descriptor.dig(:connectivity, :router_directions) || Connection::VALID_DIRECTIONS
          direction = connection.router_direction
          if direction && !valid_directions.include?(direction)
            errors << "XP #{connection.from}: invalid direction '#{direction}' for connection to #{connection.to}"
          elsif connection.router_link? && (direction.nil? || direction.empty?)
            errors << "XP #{connection.from}: missing router direction for connection to #{connection.to}"
          end
        end

        source_router_bus = from_xp.module_descriptor.dig(:connectivity, :router_bus_type)
        target_router_bus = to_xp.module_descriptor.dig(:connectivity, :router_bus_type)
        if source_router_bus && target_router_bus && source_router_bus != target_router_bus
          errors << "XP #{connection.from}: router bus '#{source_router_bus}' cannot connect to XP #{connection.to} bus '#{target_router_bus}'"
        end

        errors.concat(validate_router_port(from_xp, connection.from_port, connection.to, 'output'))
        errors.concat(validate_router_port(to_xp, connection.to_port, connection.from, 'input'))
      end

      errors
    end
  end

  private

  def validate_router_direction(connection)
    return [] unless connection.router_link?

    source_side = Connection.side_from_port(connection.from_port)
    target_side = Connection.side_from_port(connection.to_port)

    if source_side && target_side && source_side != Connection.opposite_direction(target_side)
      return [
        "XP #{connection.from}: router ports #{connection.from_port} and #{connection.to_port} imply conflicting directions for connection to #{connection.to}"
      ]
    end

    return [] if connection.dir.nil? || connection.dir.empty?

    errors = []
    if source_side && connection.dir != source_side
      errors << "XP #{connection.from}: connection direction '#{connection.dir}' does not match port #{connection.from_port}"
    end

    if target_side
      expected_direction = Connection.opposite_direction(target_side)
      if expected_direction && connection.dir != expected_direction
        errors << "XP #{connection.from}: connection direction '#{connection.dir}' does not match port #{connection.to_port}"
      end
    end

    errors
  end

  def validate_router_port(xp, port_id, peer_id, expected_direction)
    return [] unless port_id

    port = ModuleCatalog.port(xp.module_name, port_id)
    return ["XP #{xp.id}: port #{port_id} not found for connection to #{peer_id}"] unless port

    errors = []
    if port[:direction] != expected_direction
      errors << "XP #{xp.id}: port #{port_id} must be #{expected_direction} for connection to #{peer_id}"
    end

    router_bus = xp.module_descriptor.dig(:connectivity, :router_bus_type)
    if router_bus && port[:bus_type] != router_bus
      errors << "XP #{xp.id}: port #{port_id} bus '#{port[:bus_type]}' does not match router bus '#{router_bus}'"
    end

    errors
  end
end

class ValidEndpointAttachments < DrcBase
  def check(noc)
    errors = []
    endpoint_by_id = noc.endpoints.each_with_object({}) { |endpoint, result| result[endpoint.id] = endpoint }
    attached_counts = Hash.new(0)

    noc.xps.each do |xp|
      attachment_limit = xp.module_descriptor.dig(:connectivity, :max_endpoint_attachments) || 0
      if xp.endpoints.size > attachment_limit
        errors << "XP #{xp.id}: supports at most #{attachment_limit} endpoint attachments, got #{xp.endpoints.size}"
      end

      xp.endpoints.each do |endpoint_id|
        endpoint = endpoint_by_id[endpoint_id]
        unless endpoint
          errors << "XP #{xp.id}: endpoint #{endpoint_id} not found"
          next
        end

        xp_attachment_bus = xp.module_descriptor.dig(:connectivity, :attachment_bus_type)
        endpoint_attachment_bus = endpoint.module_descriptor.dig(:connectivity, :attachment_bus_type)
        if xp_attachment_bus && endpoint_attachment_bus && xp_attachment_bus != endpoint_attachment_bus
          errors << "XP #{xp.id}: endpoint #{endpoint_id} uses attachment bus '#{endpoint_attachment_bus}', expected '#{xp_attachment_bus}'"
        end

        attached_counts[endpoint_id] += 1
      end
    end

    attached_counts.each do |endpoint_id, count|
      endpoint = endpoint_by_id[endpoint_id]
      endpoint_limit = endpoint&.module_descriptor&.dig(:connectivity, :max_xp_attachments) || 1
      next if count <= endpoint_limit

      errors << "Endpoint #{endpoint_id}: attached to #{count} XPs, expected at most #{endpoint_limit}"
    end

    errors
  end
end

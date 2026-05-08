class ConnectionXpReferences < DrcBase
  def check(noc)
    xp_ids = noc.xps.map(&:id)

    noc.connections.each_with_index.flat_map do |conn, index|
      errors = []
      errors << "Connection #{index}: unknown source XP '#{conn.from}'" unless xp_ids.include?(conn.from)
      errors << "Connection #{index}: unknown target XP '#{conn.to}'" unless xp_ids.include?(conn.to)
      errors
    end
  end
end

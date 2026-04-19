require_relative '../model/module_catalog'

class ValidXpConfig < DrcBase
  def check(noc)
    noc.xps.flat_map do |xp|
      xp.config.flat_map do |key, value|
        parameter = ModuleCatalog.config_parameter(xp.module_name, key)
        next [] unless parameter

        ModuleCatalog.validate_parameter_value(parameter, value).map do |message|
          "XP #{xp.id}: #{message}"
        end
      end
    end
  end
end

#!/usr/bin/env python3

# Converts framework module bundle XML into JSON metadata used by the editor loader pipeline.
import argparse
import copy
import json
import sys
import xml.etree.ElementTree as ET
from pathlib import Path


def bool_text(value):
    return "true" if value else "false"


def set_attr_if_present(element, key, value):
    if value is None:
        return
    if isinstance(value, str) and value == "":
        return
    if isinstance(value, bool):
        element.set(key, bool_text(value))
        return
    element.set(key, str(value))


def local_name(tag):
    return tag.split("}", 1)[-1]


def text_of(element, name, default=""):
    if element is None:
        return default
    for child in element:
        if local_name(child.tag) == name:
            return (child.text or "").strip()
    return default


def first_child(element, name):
    if element is None:
        return None
    for child in element:
        if local_name(child.tag) == name:
            return child
    return None


def child_elements(element, name):
    if element is None:
        return []
    return [child for child in element if local_name(child.tag) == name]


def detect_parameter_type(value):
    lower = value.lower()
    if lower in {"true", "false"}:
        return "bool"
    try:
        int(value)
        return "int"
    except ValueError:
        pass
    try:
        float(value)
        return "double"
    except ValueError:
        pass
    return "string"


def normalize_ipxact_direction(value):
    normalized = (value or "").strip().lower()
    if normalized == "in":
        return "input"
    if normalized == "out":
        return "output"
    if normalized in {"input", "output", "inout"}:
        return normalized
    return "inout"


def parse_bool_string(value):
    normalized = (value or "").strip().lower()
    if normalized in {"true", "1", "yes"}:
        return True
    if normalized in {"false", "0", "no"}:
        return False
    return value


def load_parameter_choices(parameter_el):
    choices = []
    choices_parent = first_child(parameter_el, "choices")
    if choices_parent is None:
        return choices

    for choice_el in child_elements(choices_parent, "choice"):
        choice = dict(choice_el.attrib)
        if choice:
            choices.append(choice)
    return choices


def vendor_extension_child(element, name):
    return first_child(first_child(element, "vendorExtensions"), name)


def load_presentation_extension(module_extension):
    presentation_el = first_child(module_extension, "presentation")
    if presentation_el is None:
        return {}

    presentation = dict(presentation_el.attrib)
    graphics = {}
    for child in presentation_el:
        child_name = local_name(child.tag)
        if child_name in {"expanded", "collapsed", "arrangement"}:
            graphics[child_name] = dict(child.attrib)

    if graphics:
        presentation["graphics"] = graphics
    return presentation


def load_json_modules(path):
    data = json.loads(Path(path).read_text(encoding="utf-8"))
    modules = []
    modules_by_name = {}

    for module in data.get("modules", []):
        entry = {
            "name": module.get("name", ""),
            "palette_label": module.get("palette_label", ""),
            "graph_group": module.get("graph_group", ""),
            "description": module.get("description", ""),
            "identity": module.get("identity", {}),
            "capabilities": module.get("capabilities", {}),
            "presentation": module.get("presentation", {}),
            "ports": module.get("ports", []),
            "parameters": module.get("parameters", []),
            "config_zone": module.get("config_zone"),
        }
        modules.append(entry)
        if entry["name"]:
            modules_by_name[entry["name"]] = entry

    return modules, modules_by_name


def apply_module_presentations(modules_by_name, path):
    tree = ET.parse(path)
    root = tree.getroot()
    if local_name(root.tag) != "module-presentations":
        raise ValueError(f"{path} is not a module-presentations XML file")

    for module_el in root.findall("./"):
        if local_name(module_el.tag) != "module":
            continue
        module_name = module_el.get("type") or module_el.get("name")
        if not module_name or module_name not in modules_by_name:
            continue
        target = modules_by_name[module_name]
        for child in module_el:
            child_name = local_name(child.tag)
            if child_name == "graphics":
                target["graphics_xml"] = copy.deepcopy(child)
            elif child_name == "config-zone":
                target["config_zone_xml"] = copy.deepcopy(child)


def load_bundle_xml(path):
    tree = ET.parse(path)
    root = tree.getroot()
    if local_name(root.tag) != "module-bundle":
        raise ValueError(f"{path} is not a module-bundle XML file")

    modules = []
    for module_el in root.findall("./"):
        if local_name(module_el.tag) != "module":
            continue

        entry = {
            "name": module_el.get("name", ""),
            "palette_label": module_el.get("palette_label", ""),
            "graph_group": module_el.get("graph_group", ""),
            "description": module_el.get("description", ""),
            "identity": {},
            "capabilities": {},
            "ports": [],
            "parameters": [],
            "config_zone_xml": None,
            "graphics_xml": None,
        }

        for child in module_el:
            child_name = local_name(child.tag)
            if child_name == "identity":
                entry["identity"] = dict(child.attrib)
            elif child_name == "capabilities":
                entry["capabilities"] = dict(child.attrib)
            elif child_name == "ports":
                for port_el in child:
                    if local_name(port_el.tag) != "port":
                        continue
                    entry["ports"].append(dict(port_el.attrib))
            elif child_name == "parameters":
                for parameter_el in child:
                    if local_name(parameter_el.tag) != "parameter":
                        continue
                    parameter = dict(parameter_el.attrib)
                    choices = load_parameter_choices(parameter_el)
                    if choices:
                        parameter["choices"] = choices
                    entry["parameters"].append(parameter)
            elif child_name == "config-zone":
                entry["config_zone_xml"] = copy.deepcopy(child)
            elif child_name == "graphics":
                entry["graphics_xml"] = copy.deepcopy(child)

        modules.append(entry)

    return modules


def load_ipxact_component(path):
    tree = ET.parse(path)
    root = tree.getroot()
    if local_name(root.tag) != "component":
        raise ValueError(f"{path} is not an IP-XACT component XML file")

    name = text_of(root, "name")
    module = {
        "name": name,
        "palette_label": name,
        "graph_group": "",
        "description": text_of(root, "description"),
        "identity": {},
        "capabilities": {},
        "ports": [],
        "parameters": [],
        "config_zone": None,
    }

    module_extension = vendor_extension_child(root, "module")
    if module_extension is not None:
        module["palette_label"] = module_extension.get("palette_label", name)
        module["graph_group"] = module_extension.get("graph_group", "")
        identity = first_child(module_extension, "identity")
        capabilities = first_child(module_extension, "capabilities")
        if identity is not None:
            module["identity"] = dict(identity.attrib)
        if capabilities is not None:
            module["capabilities"] = dict(capabilities.attrib)
        presentation = load_presentation_extension(module_extension)
        if presentation:
            module["presentation"] = presentation

    model = first_child(root, "model")
    ports_parent = first_child(model, "ports")
    if ports_parent is not None:
        for port_el in ports_parent:
            if local_name(port_el.tag) != "port":
                continue
            wire = first_child(port_el, "wire")
            direction = normalize_ipxact_direction(text_of(wire, "direction", "inout"))
            port_extension = vendor_extension_child(port_el, "port")
            module["ports"].append({
                "id": text_of(port_el, "name"),
                "name": port_extension.get("label", text_of(port_el, "name")) if port_extension is not None else text_of(port_el, "name"),
                "direction": direction,
                "type": port_extension.get("frontend_type", "signal") if port_extension is not None else "signal",
                "description": text_of(port_el, "description"),
                "role": port_extension.get("role", "") if port_extension is not None else "",
                "bus_type": port_extension.get("bus_type", "") if port_extension is not None else "",
            })

    parameters_parent = first_child(root, "parameters")
    if parameters_parent is not None:
        for parameter_el in parameters_parent:
            if local_name(parameter_el.tag) != "parameter":
                continue
            value = text_of(parameter_el, "value")
            parameter_extension = vendor_extension_child(parameter_el, "parameter")
            parameter = {
                "name": text_of(parameter_el, "name"),
                "type": parameter_extension.get("frontend_type", detect_parameter_type(value)) if parameter_extension is not None else detect_parameter_type(value),
                "default": value,
                "description": text_of(parameter_el, "description"),
            }
            if parameter_extension is not None:
                for attr_name in ("label", "unit", "minimum", "maximum"):
                    if attr_name in parameter_extension.attrib:
                        parameter[attr_name] = parameter_extension.get(attr_name)
                for attr_name in ("configurable", "read_only"):
                    if attr_name in parameter_extension.attrib:
                        parameter[attr_name] = parse_bool_string(parameter_extension.get(attr_name))
                choices = []
                for choice_el in child_elements(parameter_extension, "choice"):
                    choices.append(dict(choice_el.attrib))
                if choices:
                    parameter["choices"] = choices
            module["parameters"].append(parameter)

    return [module]


def build_core_tree(modules):
    root = ET.Element("module-bundle")

    for module in modules:
        module_el = ET.SubElement(root, "module")
        set_attr_if_present(module_el, "name", module.get("name"))
        set_attr_if_present(module_el, "palette_label", module.get("palette_label"))
        set_attr_if_present(module_el, "graph_group", module.get("graph_group"))
        set_attr_if_present(module_el, "description", module.get("description"))

        identity = module.get("identity") or {}
        if identity:
            identity_el = ET.SubElement(module_el, "identity")
            for key, value in identity.items():
                set_attr_if_present(identity_el, key, value)

        capabilities = module.get("capabilities") or {}
        if capabilities:
            capabilities_el = ET.SubElement(module_el, "capabilities")
            for key, value in capabilities.items():
                set_attr_if_present(capabilities_el, key, value)

        ports = module.get("ports") or []
        if ports:
            ports_el = ET.SubElement(module_el, "ports")
            for port in ports:
                port_el = ET.SubElement(ports_el, "port")
                for key in ("id", "direction", "type", "name", "description", "role", "bus_type"):
                    set_attr_if_present(port_el, key, port.get(key))

        parameters = module.get("parameters") or []
        if parameters:
            parameters_el = ET.SubElement(module_el, "parameters")
            for parameter in parameters:
                parameter_el = ET.SubElement(parameters_el, "parameter")
                for key in ("name", "type", "default", "label", "description",
                            "configurable", "read_only", "minimum", "maximum",
                            "min", "max", "unit"):
                    if key in parameter:
                        set_attr_if_present(parameter_el, key, parameter.get(key))
                if parameter.get("choices"):
                    choices_el = ET.SubElement(parameter_el, "choices")
                    for choice in parameter["choices"]:
                        choice_el = ET.SubElement(choices_el, "choice")
                        set_attr_if_present(choice_el, "value", choice.get("value"))
                        set_attr_if_present(choice_el, "label", choice.get("label"))

        if module.get("config_zone_xml") is not None:
            module_el.append(copy.deepcopy(module["config_zone_xml"]))
        else:
            config_zone = module.get("config_zone")
            if config_zone:
                config_el = ET.SubElement(module_el, "config-zone")
                fields = config_zone.get("fields", config_zone) if isinstance(config_zone, dict) else config_zone
                for field in fields:
                    field_el = ET.SubElement(config_el, "field")
                    if isinstance(field, str):
                        field_el.set("parameter", field)
                    else:
                        for key in ("parameter", "label", "description"):
                            set_attr_if_present(field_el, key, field.get(key))

    return ET.ElementTree(root)


def graphics_element_for_module(module):
    if module.get("graphics_xml") is not None:
        return copy.deepcopy(module["graphics_xml"])

    presentation = module.get("presentation") or {}
    if not presentation:
        return None

    graphics_el = ET.Element("graphics")
    for key in ("layout", "node_color", "supports_collapse"):
        if key in presentation:
            set_attr_if_present(graphics_el, key, presentation[key])

    graphics = presentation.get("graphics", {})
    for section_name in ("expanded", "collapsed", "arrangement"):
        section_data = graphics.get(section_name)
        if not section_data:
            continue
        section_el = ET.SubElement(graphics_el, section_name)
        for key, value in section_data.items():
            set_attr_if_present(section_el, key, value)

    return graphics_el if graphics_el.attrib or list(graphics_el) else None


def write_xml(tree, path):
    ET.indent(tree, space="  ")
    output_path = Path(path)
    output_path.parent.mkdir(parents=True, exist_ok=True)
    tree.write(output_path, encoding="utf-8", xml_declaration=True)


def write_split_bundle(modules, output_dir):
    output_root = Path(output_dir)
    core_path = output_root / "modules.xml"
    graphics_dir = output_root / "graphics"

    write_xml(build_core_tree(modules), core_path)

    for module in modules:
        graphics_el = graphics_element_for_module(module)
        if graphics_el is None or not module.get("name"):
            continue
        wrapper = ET.Element("module-graphics")
        wrapper.set("type", module["name"])
        wrapper.append(graphics_el)
        write_xml(ET.ElementTree(wrapper), graphics_dir / f"{module['name']}.xml")

    print(f"Wrote {core_path}")
    if graphics_dir.exists():
        print(f"Wrote {graphics_dir}")


def main():
    parser = argparse.ArgumentParser(
        description="Convert authored JSON, IP-XACT, or module-bundle XML into the split XML IP-core bundle format."
    )
    parser.add_argument("--json", help="Hand-authored JSON bundle input")
    parser.add_argument("--xml", help="Existing module-bundle XML input")
    parser.add_argument("--ipxact", help="IEEE IP-XACT component XML input")
    parser.add_argument("--ui", help="Optional module-presentations XML overlay for JSON input")
    parser.add_argument("--output-dir", required=True, help="Directory that will receive modules.xml and graphics/*.xml")
    args = parser.parse_args()

    source_count = sum(bool(value) for value in (args.json, args.xml, args.ipxact))
    if source_count != 1:
        print("error: provide exactly one of --json, --xml, or --ipxact", file=sys.stderr)
        return 1

    try:
        if args.json:
            modules, modules_by_name = load_json_modules(args.json)
            if args.ui:
                apply_module_presentations(modules_by_name, args.ui)
        elif args.xml:
            modules = load_bundle_xml(args.xml)
        else:
            modules = load_ipxact_component(args.ipxact)

        write_split_bundle(modules, args.output_dir)
        return 0
    except Exception as exc:
        print(f"error: {exc}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    raise SystemExit(main())

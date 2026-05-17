require 'fileutils'
require 'json'
require 'optparse'

module IpcraftGenerator
  class Error < StandardError; end

  class CLI
    def self.run(argv)
      options = {}

      OptionParser.new do |parser|
        parser.on('--manifest PATH', 'Ipcraft manifest JSON path') { |value| options[:manifest] = value }
        parser.on('--input PATH', 'Ipcraft project input JSON path') { |value| options[:input] = value }
        parser.on('--output DIR', 'Generated output directory') { |value| options[:output] = value }
      end.parse!(argv)
    rescue OptionParser::ParseError => error
      raise Error, error.message
    else
      raise Error, "unexpected argument: #{argv.first}" unless argv.empty?

      raise Error, '--manifest is required' unless options[:manifest]
      raise Error, '--input is required' unless options[:input]
      raise Error, '--output is required' unless options[:output]

      Generator.new(
        manifest: options.fetch(:manifest),
        input: options.fetch(:input),
        output: options.fetch(:output)
      ).generate

      puts "Generated ipcraft output in #{options.fetch(:output)}"
    end
  end

  class Generator
    def initialize(manifest:, input:, output:)
      @manifest_path = manifest
      @input_path = input
      @output_dir = output
    end

    def generate
      manifest = JSON.parse(File.read(@manifest_path))
      input = JSON.parse(File.read(@input_path))

      validate!(manifest, input)

      FileUtils.mkdir_p(@output_dir)
      File.write(File.join(@output_dir, 'manifest.json'), "#{JSON.pretty_generate(output_manifest(manifest, input))}\n")
    end

    private

    def validate!(manifest, input)
      raise Error, 'manifest schema must be ipcraft.manifest.v1' unless manifest['schema'] == 'ipcraft.manifest.v1'
      raise Error, 'input schema must be ipcraft.noc.project.v1' unless input['schema'] == 'ipcraft.noc.project.v1'
      raise Error, 'input package does not match manifest id' unless input['package'] == manifest['id']
    end

    def output_manifest(manifest, input)
      {
        ipcore: manifest.fetch('id'),
        schema: input.fetch('schema'),
        instance_count: input.fetch('instances', []).size,
        connection_count: input.fetch('connections', []).size
      }
    end
  end
end

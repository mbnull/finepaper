class Connection
  attr_reader :from, :to, :dir

  def initialize(from, to, dir)
    @from = from
    @to = to
    @dir = dir
  end
end

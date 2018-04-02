class Bootstrap
  NUMBOOT = 1000

  def self.bootstrap(data)
    NUMBOOT.times.map do
      data.size.times.map do
        data.sample
      end
    end
  end

  def self.bootmean(data)
    data.map do |row|
      row.sum.to_f / row.size
    end
  end

  def self.continuousci(p, data)
    aux = data.sort

    # determine how many positions we will discard at each end
    pos = NUMBOOT * (1 - p) / 2 + 0.5

    # calculate the mean of the trimmed vector
    trimmed = pos.upto(NUMBOOT - pos)

    {
      mean: trimmed.sum.to_f / trimmed.size,
      ubound: trimmed.last,
      lbound: trimmed.first
    }
  end
end

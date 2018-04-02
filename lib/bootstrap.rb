class Bootstrap
  NUMBOOT = 1000
  P = 0.95

  def self.bootstrap(data)
    data = self.resampling(data)
    data = self.bootmean(data).sort

    # determine how many positions we will discard at each end
    from = NUMBOOT * (1 - P) / 2 + 0.5
    to   = NUMBOOT - from

    # calculate the mean of the trimmed vector
    trimmed = data[from..to]

    {
      mean: trimmed.sum.to_f / trimmed.size,
      ubound: trimmed.last,
      lbound: trimmed.first
    }
  end

  private

  def self.resampling(data)
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
end

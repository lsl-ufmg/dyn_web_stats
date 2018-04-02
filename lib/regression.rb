class Regression
  def self.regression(xvec, yvec)
    out = { b0: 0.0, b1: 0.0, r2: 1.0, d: 0.0 }

    sumxi = sumyi = sumxiyi = sumxi2 = sumyi2 = 0.0
    size = xvec.size

    if size == 0
      return out
    elsif size == 1
      out[:b0] = yvec[0]
      return out
    end

    xvec.zip(yvec).each do |xi, yi|
      sumxi   += xi
      sumyi   += yi
      sumxi2  += xi * xi
      sumyi2  += yi * yi
      sumxiyi += xi * yi
    end

    out[:b1] = (sumxi * sumyi - size * sumxiyi) / (sumxi * sumxi - size * sumxi2)
    out[:b0] = (sumyi - out[:b1] * sumxi) / size

    xvec.zip(yvec).each do |xi, yi|
      u = out[:b1] * xi + out[:b0]
      out[:d] += (yi - u) ** 2
    end

    out[:r2] = 1 - out[:d] / (sumyi2 - (sumyi ** 2) / size)
    return out
  end
end

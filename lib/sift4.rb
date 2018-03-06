class Sift4
  def self.calculate(content1, content2, start, finish)
    s1     = content1.size
    start1 = (s1 * start).to_i
    end1   = (s1 * finish).to_i

    s2     = content2.size
    start2 = (s2 * start).to_i
    end2   = (s2 * finish).to_i

    delta = content1[start1..end1].bytes - content2[start2..end2].bytes

    delta / content1[start1..end1].size.to_f
  end
end

class Dyn
  def self.sched(pages, capacity)
    [
      pages.order_by(sched_val: "desc").limit(capacity),
      pages.order_by(sched_val: "desc").skip(capacity),
    ]
  end

  def self.priority
    2 ** 32
  end
end

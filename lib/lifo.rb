class Lifo
  def self.sched pages, capacity
    [
      pages.order_by(previous_collection_t: "desc").limit(capacity),
      pages.order_by(previous_collection_t: "desc").skip(capacity),
    ]
  end

  def self.priority
    2 ** 32
  end
end

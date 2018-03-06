class Fifo
  def self.sched pages, capacity
    [
      pages.order_by(previous_collection_t: "asc").limit(capacity),
      pages.order_by(previous_collection_t: "asc").skip(capacity),
    ]
  end

  def self.priority
    2 ** 32
  end
end

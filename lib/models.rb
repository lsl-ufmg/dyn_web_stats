require 'mongoid'

class PagesCrawl
  include Mongoid::Document

  belongs_to :crawl
  belongs_to :page

  field :collection_t, type: Integer
end

class Crawl
  include Mongoid::Document

  belongs_to :config
  has_many :pages_crawl
end

class Page
  include Mongoid::Document

  has_many :pages_crawl
  belongs_to :config

  field :url,                   type: String
  field :content,               type: String
  field :distance,              type: Float,   default: nil
  field :size,                  type: Array,   default: []
  field :code,                  type: Array,   default: []
  field :crawl_status,          type: Integer, default: 0
  field :previous_collection_t, type: Integer
  field :next_collection_t,     type: Integer
  field :regression,            type: Hash, default: { b0: 0.0, b1: 0.0, d: 0.0, r2: 0.0 }
  field :bootstrap,             type: Hash, default: { mean: 0.0, lbound: 0.0, ubound: 0.0 }
  field :sched_val,             type: Float, default: 0.0
end

class Config
  include Mongoid::Document

  has_many :crawls
  has_many :pages

  validates_uniqueness_of :name

  field :capacity, type: Integer
  field :instant,  type: Integer
  field :name,     type: String
  field :seeds,    type: Array, default: []
end


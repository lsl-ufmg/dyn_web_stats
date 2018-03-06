require 'mongoid'

class Crawl
  include Mongoid::Document

  belongs_to :config
  has_and_belongs_to_many :pages

  field :collection_t, type: Integer
end

class Page
  include Mongoid::Document

  has_and_belongs_to_many :crawls
  belongs_to :config

  field :url,                   type: String
  field :content,               type: String
  field :distance,              type: Float,   default: nil
  field :size,                  type: Array,   default: []
  field :crawl_status,          type: Integer, default: 0
  field :previous_collection_t, type: Integer
  field :next_collection_t,     type: Integer
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


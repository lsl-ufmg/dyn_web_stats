require 'models'
require 'heritrix'
require 'warc'
require 'fifo'
require 'dyn'
require 'sift4'
require 'bootstrap'
require 'regression'

class DynWebStats
  FACTOR = 2

  def self.configs mongoid_config
    DynWebStats.load_mongoid_config mongoid_config
    Config.all
  end

  def self.new_config mongoid_config, capacity, name, seeds
    DynWebStats.load_mongoid_config mongoid_config
    config = Config.create!(capacity: capacity, instant: 1, name: name, seeds: seeds)

    # coloca as seeds no fluxo normal de coleta
    seeds.each do |seed|
      Page.create(url: seed, previous_collection_t: 0, next_collection_t: 1, config: config)
    end
  end

  def initialize(mongoid_config, job_name: "mapaweb", sched: :fifo, path:, config_name:)
    DynWebStats.load_mongoid_config mongoid_config

    @pages = []
    @crawl_list = []
    @path = path
    @job_name = job_name
    @heritrix = Heritrix.new(@path, @job_name)
    @warc_path = "#{@path}/jobs/#{@job_name}/latest/warcs"
    @warc_bin_path = "#{@path}/../extras/warc"

    begin
      @config = Config.find_by(name: config_name)
    rescue Mongoid::Errors::DocumentNotFound
      puts "Configuração de nome '#{config_name}' não encontrada!"
      exit -1
    end

    case sched
    when :fifo
      @scheduler = Fifo
    when :lifo
      @scheduler = Lifo
    when :dyn
      @scheduler = Dyn
    else
      raise "Invalid scheduler"
    end
  end

  def run
    create_crawl
    get_pages_to_crawl
    scheduler
    @heritrix.update_seeds(@pages.pluck(:url))
    @heritrix.start_heritrix
    @heritrix.run_job
    @heritrix.stop_heritrix
    parse_warcs
    process_new_pages
    process_pages
    update_config
  end

  def update_config
    @config.update_attribute(:instant, @config.instant + 1)
  end

  def process_pages
    File.open("#{@warc_path}/0/resultados.json").each_line do |line|
      json    = JSON.parse(line)
      content = json["content"]
      url     = json["WARC-Target-URI"]
      size    = json["Content-Length"].to_i

      next if ignore_pages url

      page = Page.where(url: url).last || Page.create(url: url, config: @config, crawl: @crawl)

      last_size = page.size.last.to_i
      page.size << size

      min, max = [size, last_size].minmax

      prop = 1 - min.to_f / max.to_f

      page.previous_collection_t = @config.instant
      old_content = page.content
      page.content = content

      new_instant = @config.instant

      if old_content.nil? # Nunca foi coletada
        new_instant += 1
      elsif prop < 0.15 # Não tem diferença de tamanho significativa
        new_instant *= FACTOR
      elsif Sift4.calculate(content, old_content, 0.65, 0.90) > 0 # Mudou
        new_instant += new_instant / FACTOR
      end

      page.next_collection_t = new_instant

      x = page.crawls.map(&:collection_t)
      page.regression = Regression.regression(x, page.size)
      page.bootstrap = Bootstrap.bootstrap(page.size)

      if @scheduler == Dyn
        u = page.regression[:b1] * @config.instant + page.regression[:b0]

        outdatecost = (u - page.bootstrap[:mean]).abs / page.bootstrap[:mean]
        variancecost = (page.bootstrap[:ubound] - page.bootstrap[:mean]).abs / page.bootstrap[:mean]

        page.sched_val = 2 * outdatecost * variancecost / (outdatecost + variancecost)
      end

      page.save!
    end

    Page.where(next_collection_t: @config.instant).update_all(next_collection_t: @config.instant * FACTOR)
  end

  def process_new_pages
    lista = []

    #db.pages.createIndex({"url": 1}, { unique: true})
    File.read("#{@warc_path}/0/metadata").each_line do |line|
      next if ignore_pages line
      lista << { url: line.chomp, previous_collection_t: @scheduler.priority, sched_val: @scheduler.priority, next_collection_t: @config.instant + 1, config_id: @config.id }
    end

    begin
      Page.collection.insert_many(lista, { ordered: false })
    rescue Mongo::Error::BulkWriteError # Existing pages
    end
  end

  def ignore_pages url
    rules = [
      /\.(avi|wmv|mpe?g|mp3|rar|zip|tar|gz|swf|pdf|doc|xls|odt)\z/,
      /\.(xml|txt|conf|pdf|js|css|bmp|gif|jpe?g|png|svg|tiff?)\z/,
      /robots\.txt\z/
    ]

    rules.each {|r| return true if(url =~ r)}
    false
  end

  def parse_warcs
    Warc.parse(@warc_bin_path, @warc_path)
  end

  def create_crawl
    @crawl = Crawl.create!(collection_t: @config.instant, config: @config)
  end

  # gera estrutura interna de coleta a partir do db
  def get_pages_to_crawl
    @pages = @config.pages.where(next_collection_t: @config.instant)
  end

  # decide o que coletar
  def scheduler
    capacity = @config[:capacity]

    @crawl_list, @remainer = @scheduler.sched(@pages, capacity)

    if @remainer.any?
      @remainer.update_all(postpone: true)
    else
      postponed = @crawl.pages.where(postpone: true)
      @postponed_list, _ = @scheduler.sched(postponed, capacity - @crawl_list.size)
    end

    #TODO otimizar
    @pages = @crawl_list.to_a + @postponed_list.to_a
    @crawl.pages = @pages
  end

  def self.load_mongoid_config config
    Mongoid.load!(config, :development)
  end
end

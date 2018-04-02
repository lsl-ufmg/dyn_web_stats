require 'models'
require 'heritrix'
require 'warc'
require 'fifo'
require 'dyn'
require 'sift4'
require 'bootstrap'
require 'regression'

# escalonador
# 1. instanciar variáveis iniciais (qual o T, qual a capacidade C, etc)
# 2. busca no banco as páginas agendadas para T
# 3. verifica se ultrapassa a capacidade C
# 4. caso sim: temos que postergar páginas -> usamos política de escalonamento
# 5. caso não: vemos se tem páginas postergadas e adiantamos sua coleta
# 6. coleta as páginas
# 7. após coleta, processa indicadores, atualiza banco com dados de mudança e novas datas de agendamento
#
# as busca no banco já retornam os dados corretamente.
# O yuri já fez as partes de processamento (extração de links e mudança de páginas)
#
# entrada (vem do db)
# saida atualização do banco
# saida eh uma lista de urls que serão coletadas

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
    # update mongo(next_collection, previous_collect)
    # pega a estrutura interna, roda scheduler e gera arquivo de coleta
  end

  def process_pages
    File.open("#{@warc_path}/0/resultados.json").each_line do |line|
      json    = JSON.parse(line)
      content = json["content"]
      url     = json["WARC-Target-URI"]
      size    = json["Content-Length"].to_i

      next if ignore_pages url

      page = Page.where(url: url).last

      if page.nil?
        puts "Página '#{url}' não existe no banco!"
        next
      end

      last_size = page.size.last.to_i
      page.size << size

      min, max = [size, last_size].minmax

      prop = 1 - min.to_f / max.to_f

      page.update_attribute(:previous_collection_t, @config.instant)
      old_content = page.content
      page.update_attribute(:content, content)

      new_instant = @config.instant

      if old_content.nil? # Nunca foi coletada
        new_instant += 1
      elsif prop < 0.15 # Não tem diferença de tamanho significativa
        new_instant *= FACTOR
      elsif Sift4.calculate(content, old_content, 0.65, 0.90) > 0 # Mudou
        new_instant += new_instant / FACTOR
      end

      page.update_attribute(:next_collection_t, new_instant)

      x = page.crawls.map(&:collection_t)
      page.update_attribute(:regression, Regression.regression(x, page.size))
      page.update_attribute(:bootstrap, Bootstrap.bootstrap(page.size))

      if @scheduler == Dyn
        u = page.regression[:b1] * @config.instant + page.regression[:b0]

        outdatecost = (u - page.bootstrap[:mean]).abs / page.bootstrap[:mean]
        variancecost = (page.bootrap[:ubound] - page.bootstrap[:mean]).abs / page.bootstrap[:mean]

        page.update_attribute(:sched_val, 2 * outdatecost * variancecost / (outdatecost + variancecost))
      end
    end
  end

  def process_new_pages
    lista = []

    #db.paginas.createIndex({"url": 1}, { unique: true})
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
    rules = [/\.(avi|wmv|mpe?g|mp3)\z/, /\.(rar|zip|tar|gz)\z/,
     /\.(pdf|doc|xls|odt)\z/, /\.(xml)\z/, /\.(txt|conf|pdf)\z/,
     /\.(swf)\z/, /\.(js|css)\z/, /\.(bmp|gif|jpe?g|png|svg|tiff?)\z/,
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

# primeira coleta  -> cria estrutura interna de coleta -> scheduler -> run -> pós processa resultado
# proximas coletas -> pega dados do db e coloca na estrutura de coleta -> scheduler -> run -> pós processa resultado

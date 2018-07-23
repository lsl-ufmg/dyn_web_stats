require 'open3'
require 'logger'

class Heritrix
  URL = "https://localhost:8443/engine/job/"
  MAX_TRIES = 50
  WAIT_SEC = 5

  def initialize path, job_name, auth: "admin:admin", bind: "0.0.0.0"
    @path = path
    @job_name = job_name
    @url = URL + job_name
    @auth = auth
    @bind = bind
    @pid = nil

    @logger = Logger.new(STDOUT)
    @logger.level = Logger::INFO
  end

  def update_seeds(seeds)
    file = File.join(@path, "jobs", @job_name, "seeds.txt")

    File.open(file, "w") do |f|
      seeds.each do |seed|
        f << seed << "\n"
      end
    end
  end

  def start_heritrix
    unless @pid
      bin = File.join(@path, "bin", "heritrix")
      result = `#{bin} -a #{@auth} -b #{@bind}`

      @pid = (result.match /\(pid (\d*)\)/)[1].to_i
    end
  end

  def stop_heritrix
    loop do
      begin
        Process.kill('SIGTERM', @pid)
      rescue Errno::ESRCH
        @logger.info("Heritrix stopped with success.")
        return
      else
        @logger.info("Waiting for Heritrix to stop.")
        sleep 5
      end
    end
  end

  def stop_job
    teardown_job
  end

  def teardown_job
    run_and_wait_action("teardown", "Job is Unbuilt", exit_on_timeout: false)
  end

  def start_job
    start_heritrix if @pid.nil?
    run_and_wait_action("launch", "Job is Active: RUNNING")
  end

  def run_job
    start_job

    while status = get_job_status
      @logger.info(
        "Job #{@job_name} running: #{status[:downloaded]}/#{status[:total]} (#{status[:prop]}%)"
      )
      break if status[:prop] >= 90.00
      sleep WAIT_SEC
    end

    stop_job
  end

  def get_job_status
    out = `curl -s -k -u #{@auth} --digest --location #{@url}`

    if out["Job is Active: RUNNING"]
      matched = out.match(/([0-9.]+) downloaded \+ ([0-9.]+) queued = ([0-9.]+) total/)

      ret = {
        downloaded: matched[1].tr('.','').to_i,
        queued: matched[2].tr('.','').to_i,
        total: matched[3].tr('.','').to_i,
      }

      ret[:prop] = (100 * ret[:downloaded] / ret[:total].to_f).round(2)

      ret
    else
      false
    end
  end

  private

  def run_action(action, msg: nil)
    out = `curl -s -d "action=#{action}" -k -u #{@auth} --digest --location #{@url}`

    !msg || (msg && out[msg] != nil)
  end

  def run_and_wait_action(action, msg, exit_on_timeout: true)
    tries = 0

    while !run_action(action, msg: msg)
      tries += 1

      @logger.info("Waiting for '#{action}' to be executed on '#{@job_name}'.")

      if tries == MAX_TRIES
        @logger.error("Could not execute '#{action}' on '#{@job_name}'. MAX_TRIES (#{MAX_TRIES}) reached.");
        exit if exit_on_timeout
        return
      end

      sleep WAIT_SEC
    end

    @logger.info("Action '#{action}' executed on '#{@job_name}' with success.")
  end
end

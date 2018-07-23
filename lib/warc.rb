class Warc
  def self.parse(bin_path, path)
    `touch #{path}/universe`
    `mkdir #{path}/0`

    `zcat #{path}/* | #{bin_path}/warc -t 50000000 -j -d 0 -p #{path}`

    `sort -u #{path}/0/metadata > #{path}/0/newMetadata`
    `mv #{path}/0/newMetadata #{path}/0/metadata`

    `iconv -f latin1 -t utf-8 #{path}/0/resultados.json > #{path}/0/newResults.json`
    `mv #{path}/0/newResults.json #{path}/0/resultados.json`
  end
end

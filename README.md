# DynWebStats

## Descrição estrutural

Os seguintes arquivos são utilizados no sistema:

|         Arquivo        |                                  Descrição                                            |
|:-----------------------|:--------------------------------------------------------------------------------------|
| lib/dyn\_web\_stats.rb | Código central, responsável por fazer todas as chamadas e gerência dos demais códigos |
| lib/fifo.rb            | Implementação do escalonador FIFO |
| lib/lifo.rb            | Implementação do escalonador LIFO |
| lib/heritrix.rb        | Código que encapsula a comunicação com o daemon do Heritrix |
| lib/models.rb          | Define os models do MongoDB utilizados no processo de coleta |
| lib/sift4.rb           | Implementação simplificada da distância de edição sift4 (semelhante à distância Levenshtein) |
| lib/warc.rb            | Código que chama funções de parsear os arquivos WARC gerados pelo heritrix                   |
| extras/warc            | Implementação em C do parser de WARC, utilizada no código acima |

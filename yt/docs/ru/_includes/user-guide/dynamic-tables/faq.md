#### **Q: При работе с динамическими таблицами из Python API или C++ API  получаю ошибку "Sticky transaction 1935-3cb03-4040001-e8723e5a is not found", что делать?**

**A:** Ответ зависит от того, как используются транзакции. Если используется [мастерная транзакция](../../../user-guide/dynamic-tables/transactions.md), то это бессмысленное действие, в таком случае необходимо задавать запрос вне транзакции, для этого можно либо создать отдельный клиент, либо явно указать, что запускаться под нулевой транзакций (`with client.Transaction(transaction_id="0-0-0-0"): ...`).
Полноценное использование таблетных транзакций в Python API возможно только через RPC-proxy (`yt.config['backend'] = 'rpc'`). Использование таблетных транзакций через HTTP невозможно в текущей реализации.

------
#### **Q: При записи в динамическую таблицу возникает ошибка "Node is out of tablet memory; all writes disabled" или "Active store is overflown, all writes disabled". Что она означает и как с ней бороться?**

**A:** Ошибка возникает, когда на узле кластера заканчивается память для хранения данных, не записанных на диск. Входной поток данных слишком велик, узел кластера не успевает выполнять сжатие и запись на диск. Запросы с подобными ошибками необходимо повторять, возможно с увеличивающейся задержкой. Если ошибка возникает постоянно, то это может означать (помимо случаев нештатной работы), что либо отдельные таблеты перегружены нагрузкой по записи, либо в целом мощности кластера не достаточно для того, чтобы справиться с данной нагрузкой. Также может помочь увеличение количества таблетов в таблице (команда `reshard-table`).

------
#### **Q: Что значит алерт "Too many overlapping stores"? Что делать?**

**A:** Данное сообщение об ошибке значит, что структура таблета такова, что покрытие сторами обслуживаемого таблетом интервала ключей слишком плотное. Плотное покрытие сторами ведет к деградации производительности чтения, поэтому в данной ситуации включается защитный механизм, препятствующий записи новых данных. Постепенно фоновые процессы компактификации и партицирования должны нормализовать структуру таблета; если этого не происходит, то, возможно, кластер не справляется с нагрузкой.

------
#### **Q: При запросе в динамическую таблицу получаю ошибку 'Maximum block size limit violated'**

**A:** В запросе участвует динамическая таблица, когда-то сконвертированная из статической. При этом не был указан параметр `block_size`. При получении подобной ошибки пожалуйста убедитесь, что выполняете все указания из [раздела](../../../user-guide/dynamic-tables/mapreduce.md), посвящённого конвертации статической таблицы в динамическую. В случае, если размер блока получается большим (так может быть, если в ячейках таблицы хранятся больше блобы, а они целиком попадают в один блок), то следует увеличить `max_unversioned_block_size` до 32 мегабайт и перемонтировать таблицу.

------
#### **Q: При запросе в динамическую таблицу получаю ошибку 'Too many overlapping stores in tablet'**

**A:** Скорее всего, таблет не справляется с потоком записи — новые чанки не успевают компактифицироваться. Следует проверить, пошардирована ли таблица на достаточное число таблетов. При записи данных в пустую таблицу стоит отключить автошардирование, поскольку маленькие таблеты будут объединяться в один.

------
#### **Q: При запросе в динамическую таблицу получаю ошибку 'Active store is overflown, all writes disabled'**

**A:** Таблет не справляется с потоком записи — либо не успевает сбрасывать данные на диск, либо по каким-то причинам не может этого сделать. Следует посмотреть нет ли ошибок в атрибуте таблицы `@tablet_errors`, если нет, то, аналогично предыдущему пункту, проверить шардирование.

------
#### **Q: При запросе в динамическую таблицу получаю ошибку 'Too many stores in tablet, all writes disabled'**

**A:** Слишком большой таблет. Необходимо добиться, того чтобы у таблицы стало больше таблетов. Обратите внимание, что [автошардирование](../../../user-guide/dynamic-tables/resharding.md#auto) ограничивает число таблетов как число селлов помноженое на значение параметра `tablet_balancer_config/tablet_to_cell_ratio`.

------
#### **Q: При запросе в динамическую таблицу получаю ошибку 'Tablet ... is not known'**

**A:** Клиент отправил запрос на узел кластера, на котором таблет уже не обслуживается. Как правило, такое происходит в результате автоматической балансировки таблетов или перезапуска узлов кластера. Необходимо повторно отправить запрос, ошибка пройдёт сама после обновления кэша, либо отключать балансировку.

------
#### **Q: При запросе в динамическую таблицу получаю ошибку 'Service is not known'**

**A:** Клиент отправил запрос на узел кластера, на котором таблет-селл уже не обслуживается. Как правило такое происходит при перебалансировке селлов. Необходимо повторно отправить запрос, ошибка пройдёт сама после обновления кэша.

------
#### **Q: При запросе в динамическую таблицу получаю ошибку 'Chunk data is not preloaded yet'**

**A:** Сообщение характерно для таблицы с параметром `in_memory_mode`, отличным от `none`. Такая таблица в примонтированном состоянии всегда находится в памяти. Для того чтобы читать из такой таблицы, все данные должны быть подгружены в память. Если таблица была только что примонтирована, или таблет переехал в другой селл, или был перезапуск процесса {{product-name}}, то данных в памяти нет, и возникает подобная ошибка. Необходимо подождать пока фоновый процесс подгрузит данные в память.

------
#### **Q: В tablet_errors вижу ошибку 'Too many write timestamps in a versioned row' или 'Too many delete timestamps in a versioned row'**

**A:** В сортированных динамических таблицах одновременно хранится много версий одного и того же значения. В lookup формате у каждого ключа может быть не более чем 2^16 версий. В качестве простого решения можно использовать поколоночный формат (`@optimize_for = scan`). На практике такое большое число версий не нужно и они возникают вследствие неправильной конфигурации или программной ошибки. Например, при указании `atomicity=none` можно обновлять один и тот же ключ таблицы с огромной частотой (в данном режиме не происходит блокировки строк и транзакции с пересекающимся диапазоном времени могут обновлять один и тот же ключ). Так делать конечно же не стоит. Если же запись большого числа версий вызвана продуктовой необходимостью, например частые записи дельт в агрегирующих колонках, то стоит выставить атрибут таблицы `@merge_rows_on_flush=%true` и корректно [настроить удаление данных по TTL](../../../user-guide/dynamic-tables/sorted-dynamic-tables.md#remove_old_data), чтобы при флаше в чанк писалось только небольшое число реально необходимых версий.

------
#### **Q: При запросе Select Rows получаю ошибку 'Maximum expression depth exceeded'**

**A:** Такая ошибка возникает, если глубина дерева разбора выражения получается слишком большой. Как правило, такое происходит при написании выражений вида 
```FROM [...] WHERE (id1="a" AND id2="b") OR (id1="c" AND id2="d") OR ... <несколько тысяч условий>```
Вместо этого их нужно задавать в форме ```FROM [...] WHERE (id1, id2) IN (("a", "b"), ("c", "b"), ...)```
С первым вариантом есть несколько проблем. Дело в том, что запросы компилируются в машинный код. Машинный код для запросов кешируются так, что ключом служит структура запроса (без учета констант).
1. В первом случае при увеличении количество условий структура запроса будет постоянно меняться. Будет кодогенерация на каждый запрос.
2. Первый запрос будет порождать очень большое количество кода.
3. Компиляция запроса будет очень медленной.
4. Первый случай будет работать просто проверкой всех условий. Никакой трансформации в более хитрый алгоритм там не предусмотрено.
5. Кроме этого, если колонки ключевые, для них выводятся диапазоны чтения, чтобы читать только нужные данные. Алгоритм вывода диапазонов чтения будет более оптимально работать для варианта с IN.
6. В случае с IN при проверке условия будет поиск по хеш таблице.

------
#### **Q: Работе с динамической таблице получаю ошибку 'Value is too long'**

**A:** Есть довольно жесткие лимиты на размер значений в динамических таблицах. Сейчас размер одного значения (ячейки таблицы) должен не превосходить 16 мегабайт, а длина всей строки - 128 мегабайт и 512 мегабайт с учётом всех версий. Всего в строке может быть не более 1024 значений с учётом всех версий. Также есть ограничения на количество строк в запросах, которое по умолчанию равно 100000 строк в транзакции при вставке,  миллион строк при селекте и 5 миллионов при лукапах. Обратим внимание, что не стоит подбираться близко к пороговым значениям. Часть из них захардкожена и мы не сможем легко вам помочь, если вы вылезете за ограничения

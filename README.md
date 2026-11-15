<img width="2559" height="1402" alt="Screenshot_158" src="https://github.com/user-attachments/assets/2a4db9b0-1edd-4240-86f6-7d328910eff6" />

# krendtrademonitor

Десктоп-монитор торгового бота **krendeltrader** (Qt 5.12, C++17). Тот же инструментарий и стиль,
что у krendelmonitor: библиотека `QtInterface` (BaseWidget, `Create<T>()`, Theme, Logger, ApiClient, Poller)
плюс `StreamClient` — WebSocket-поток `/api/stream`. Спецификация API — `krendeltrader/MONITOR_API.md`.

## Окно

| где | что | API |
|---|---|---|
| слева | **обзорщик токена**: свечи, цена, изменение за окно; кнопки 15 мин … 7 дн; «Токены…» — сетка кнопок (сверху BTC, ETH, PEPE, ниже остальные по обороту, поиск + Enter). Для монеты бота — вход, SL, TP, ликвидация и отметки сделок | `/api/candles`, `/api/symbols`, тики из потока (подписка `subscribe`), `/api/trades`, `/api/state` |
| справа | **связь с ботом**: лента раздумий (планы с рассуждением и параметрами, события, сообщения бота), строка комментария + «Вызвать к размышлению» | `/api/plans`, `/api/events`, `POST /api/replan`, поток `plan` / `planning` / `event` / `notify` |
| снизу «Лог сервера» | хвост `bot.log` с подсветкой WARNING/ERROR, фильтр, пауза | `/api/logs?after=<offset>` |
| снизу «Рекомендации» | файлы `advice/`, «Открыть» → окно с текстом и «Сохранить» (Ctrl+S); крестик не сохраняет, при правках переспрашивает; 409 — файл изменили на сервере | `/api/advice`, `PUT /api/advice/{name}` с `mtime`, `POST /api/advice` |
| снизу «Журнал монитора» | журнал самого монитора (ошибки запросов и т. п.) | — |

Строка состояния: режим и цена бота, «Claude думает…», ошибка бота (`bot_error`), equity бота, связь с потоком
и с API. F5 — обновить всё. Раскладка окна, последняя монета и окно графика запоминаются.

Аккаунты (API биржи 1.1): бот торгует на `main`, оператор — на `manual`; заголовок `X-Account` ставит сервер бота,
монитор его не передаёт. Цены монет и список `/api/symbols` идут через `manual` — пока он не работает,
график других монет и окно «Токены» показывают ошибку / запасной список. Ошибки торгового API показываются
с кодом: ` `.

## Запуск

1. Туннель к серверу бота:
   ```bash
   ssh -N -L 8900:127.0.0.1:8900 krendel-bot
   ```
2. Сборка (MSVC из VS 18 Build Tools + Qt 5.12.12 msvc2017_64): `build.bat` → `build-rel\krendtrademonitor.exe`.
3. При первом запуске рядом с exe создаётся `trademonitor.json` — впиши `token` (= `MONITOR_TOKEN` из
   `/opt/krendeltrader/.env`). Остальное:
   ```json
   {
     "server_url": "http://127.0.0.1:8900",
     "token": "...",
     "poll_interval_ms": 3000,          // лог сервера; цена, пока поток отключён
     "candles_interval_ms": 5000,       // свечи (кеш сервера 5 с)
     "timeout_ms": 10000,
     "favorite_symbols": ["BTCUSDT", "ETHUSDT", "1000PEPEUSDT"],   // верх окна «Токены»
     "symbols": ["SOLUSDT", "..."]      // запасной список, пока сервер не собрал /api/symbols
   }
   ```

## Проверка без сервера

`tools/mock_api.py` — мок API на стандартной библиотеке Python (REST + WebSocket, подписка на тики,
replan с `planning`, рекомендации с 409/400):

```bash
python tools/mock_api.py
```

Затем запусти монитор — `server_url` по умолчанию уже `http://127.0.0.1:8900`, токен мок не проверяет.

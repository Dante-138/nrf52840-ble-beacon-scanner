/*
 * Firmware nRF52840 DK - BLE Beacon + Scanner
 *
 * Dois modos de operacao:
 *   BEACON:  Transmite pacotes iBeacon detectaveis por qualquer scanner BLE
 *   SCANNER: Escaneia dispositivos BLE proximos e mostra via UART (PuTTY)
 *
 * Botoes:
 *   Botao 1 = Alterna entre modo Beacon e Scanner
 *   Botao 2 = Inicia/para o scan (modo Scanner) ou muda UUID (modo Beacon)
 *
 * LEDs:
 *   LED1 piscando = sistema rodando
 *   LED2 aceso    = modo Beacon ativo
 *   LED3 aceso    = modo Scanner ativo
 *   LED4 pisca    = dispositivo BLE encontrado (scanner)
 */

#include <zephyr/types.h>
#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/uart.h>

#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/bluetooth/hci.h>
#include <zephyr/bluetooth/gap.h>

#include <dk_buttons_and_leds.h>

#include <stdio.h>
#include <string.h>

#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(ble_beacon_scanner);

/* ---------- Definicoes ---------- */
#define DEVICE_NAME     CONFIG_BT_DEVICE_NAME
#define DEVICE_NAME_LEN (sizeof(DEVICE_NAME) - 1)

#define RUN_STATUS_LED     DK_LED1
#define BEACON_STATUS_LED  DK_LED2
#define SCANNER_STATUS_LED DK_LED3
#define FOUND_LED          DK_LED4
#define LED_BLINK_INTERVAL 1000

#define UART_BUF_SIZE      256
#define UART_TX_TIMEOUT_MS SYS_FOREVER_MS

#define SCAN_INTERVAL      0x00A0  /* 100 ms */
#define SCAN_WINDOW        0x0050  /* 50 ms  */

#define MAX_SEEN_DEVICES   64
#define SCAN_MSG_QUEUE_SIZE 16
#define PRINT_THREAD_STACK  2048
#define PRINT_THREAD_PRIO   8

/* iBeacon constants */
#define IBEACON_COMPANY_ID   0x004C  /* Apple */
#define IBEACON_TYPE         0x0215
#define IBEACON_RSSI_AT_1M   0xC5    /* -59 dBm */

/* Modos de operacao */
enum app_mode {
	MODE_BEACON = 0,
	MODE_SCANNER,
	MODE_COUNT
};

/* ---------- Variaveis globais ---------- */
static const struct device *uart = DEVICE_DT_GET(DT_CHOSEN(nordic_nus_uart));
static enum app_mode current_mode = MODE_BEACON;
static bool scanner_active;
static bool beacon_active;
static uint16_t beacon_major = 0x0001;
static uint16_t beacon_minor = 0x0001;
static int devices_found;

/* UART TX serializado via semaforo */
static uint8_t uart_tx_buf[UART_BUF_SIZE];
static K_SEM_DEFINE(uart_tx_done, 1, 1);

/* Filtro de duplicados por software */
static bt_addr_le_t seen_devices[MAX_SEEN_DEVICES];
static int seen_count;

/* Fila de mensagens do scanner para a thread de impressao */
struct scan_msg {
	bt_addr_le_t addr;
	int8_t rssi;
	char name[32];
	bool is_beacon;
	uint16_t major;
	uint16_t minor;
	int8_t beacon_tx;
};

K_MSGQ_DEFINE(scan_msgq, sizeof(struct scan_msg), SCAN_MSG_QUEUE_SIZE, 4);

/* iBeacon UUID: E2C56DB5-DFFB-48D2-B060-D0F5A71096E0 (padrao Apple AirLocate) */
static const uint8_t ibeacon_uuid[16] = {
	0xE2, 0xC5, 0x6D, 0xB5, 0xDF, 0xFB, 0x48, 0xD2,
	0xB0, 0x60, 0xD0, 0xF5, 0xA7, 0x10, 0x96, 0xE0
};

/* ---------- iBeacon Advertising Data ---------- */

/* Manufacturer specific data for iBeacon */
static uint8_t ibeacon_mfg_data[] = {
	/* iBeacon prefix */
	0x4C, 0x00,             /* Apple Company ID (little-endian) */
	0x02, 0x15,             /* iBeacon type + length */
	/* UUID (16 bytes) */
	0xE2, 0xC5, 0x6D, 0xB5, 0xDF, 0xFB, 0x48, 0xD2,
	0xB0, 0x60, 0xD0, 0xF5, 0xA7, 0x10, 0x96, 0xE0,
	/* Major (2 bytes) */
	0x00, 0x01,
	/* Minor (2 bytes) */
	0x00, 0x01,
	/* TX Power at 1m */
	0xC5
};

static const struct bt_data beacon_ad[] = {
	BT_DATA_BYTES(BT_DATA_FLAGS, BT_LE_AD_NO_BREDR),
	BT_DATA(BT_DATA_MANUFACTURER_DATA, ibeacon_mfg_data,
		sizeof(ibeacon_mfg_data)),
};

/* Scan response com nome do dispositivo */
static const struct bt_data beacon_sd[] = {
	BT_DATA(BT_DATA_NAME_COMPLETE, DEVICE_NAME, DEVICE_NAME_LEN),
};

/* ---------- UART ---------- */
static void uart_cb(const struct device *dev, struct uart_event *evt,
		    void *user_data)
{
	ARG_UNUSED(dev);
	ARG_UNUSED(user_data);

	if (evt->type == UART_TX_DONE || evt->type == UART_TX_ABORTED) {
		k_sem_give(&uart_tx_done);
	}
}

static void uart_send(const char *fmt, ...)
{
	va_list args;
	int len;

	/* Espera TX anterior completar (timeout 500ms) */
	k_sem_take(&uart_tx_done, K_MSEC(500));

	va_start(args, fmt);
	len = vsnprintf(uart_tx_buf, sizeof(uart_tx_buf), fmt, args);
	va_end(args);

	if (len > 0 && len < (int)sizeof(uart_tx_buf)) {
		uart_tx(uart, uart_tx_buf, len, UART_TX_TIMEOUT_MS);
	} else {
		k_sem_give(&uart_tx_done);
	}
}

static int uart_init(void)
{
	int err;

	if (!device_is_ready(uart)) {
		LOG_ERR("UART nao esta pronta");
		return -ENODEV;
	}

	err = uart_callback_set(uart, uart_cb, NULL);
	if (err) {
		LOG_ERR("Erro ao configurar callback UART: %d", err);
		return err;
	}

	return 0;
}

/* ---------- Beacon ---------- */
static void update_beacon_data(void)
{
	/* Atualiza Major/Minor no payload */
	ibeacon_mfg_data[20] = (beacon_major >> 8) & 0xFF;
	ibeacon_mfg_data[21] = beacon_major & 0xFF;
	ibeacon_mfg_data[22] = (beacon_minor >> 8) & 0xFF;
	ibeacon_mfg_data[23] = beacon_minor & 0xFF;
}

static int beacon_start(void)
{
	int err;

	if (beacon_active) {
		return 0;
	}

	update_beacon_data();

/* Non-connectable scannable: permite scan response com o nome */
	static const struct bt_le_adv_param adv_param =
		BT_LE_ADV_PARAM_INIT(BT_LE_ADV_OPT_USE_IDENTITY | BT_LE_ADV_OPT_SCANNABLE,
				     BT_GAP_ADV_FAST_INT_MIN_2,
				     BT_GAP_ADV_FAST_INT_MAX_2,
				     NULL);

	err = bt_le_adv_start(&adv_param, beacon_ad,
			      ARRAY_SIZE(beacon_ad), beacon_sd,
			      ARRAY_SIZE(beacon_sd));
	if (err) {
		LOG_ERR("Erro ao iniciar beacon: %d", err);
		return err;
	}

	beacon_active = true;
	dk_set_led_on(BEACON_STATUS_LED);
	dk_set_led_off(SCANNER_STATUS_LED);

	uart_send("\r\n========== MODO BEACON ==========\r\n");
	uart_send("iBeacon ativo!\r\n");
	uart_send("UUID: E2C56DB5-DFFB-48D2-B060-D0F5A71096E0\r\n");
	uart_send("Major: %u  Minor: %u\r\n", beacon_major, beacon_minor);
	uart_send("TX Power: -59 dBm (1m referencia)\r\n");
	uart_send("==================================\r\n\r\n");

	LOG_INF("Beacon iniciado (Major=%u, Minor=%u)", beacon_major, beacon_minor);
	return 0;
}

static int beacon_stop(void)
{
	int err;

	if (!beacon_active) {
		return 0;
	}

	err = bt_le_adv_stop();
	if (err) {
		LOG_ERR("Erro ao parar beacon: %d", err);
		return err;
	}

	beacon_active = false;
	dk_set_led_off(BEACON_STATUS_LED);

	LOG_INF("Beacon parado");
	return 0;
}

/* ---------- Filtro de duplicados ---------- */
static void seen_devices_clear(void)
{
	seen_count = 0;
}

static bool seen_devices_check_add(const bt_addr_le_t *addr)
{
	/* Verifica se ja vimos este endereco */
	for (int i = 0; i < seen_count; i++) {
		if (bt_addr_le_cmp(&seen_devices[i], addr) == 0) {
			return true;  /* Ja visto */
		}
	}

	/* Adiciona na tabela se houver espaco */
	if (seen_count < MAX_SEEN_DEVICES) {
		bt_addr_le_copy(&seen_devices[seen_count], addr);
		seen_count++;
	}

	return false;  /* Novo dispositivo */
}

/* ---------- Scanner ---------- */
static const char *addr_type_str(uint8_t type)
{
	switch (type) {
	case BT_ADDR_LE_PUBLIC:
		return "pub";
	case BT_ADDR_LE_RANDOM:
		return "rnd";
	default:
		return "???";
	}
}

static bool is_ibeacon(const uint8_t *data, uint8_t len)
{
	/* Minimo para iBeacon: 4 bytes header + 16 UUID + 2 major + 2 minor + 1 tx = 25 */
	if (len < 25) {
		return false;
	}
	/* Verifica Apple Company ID e iBeacon type */
	return (data[0] == 0x4C && data[1] == 0x00 &&
		data[2] == 0x02 && data[3] == 0x15);
}

static void parse_ad_data(struct net_buf_simple *ad, char *name, size_t name_len,
			   bool *is_beacon_flag, uint16_t *major, uint16_t *minor,
			   int8_t *beacon_tx)
{
	name[0] = '\0';
	*is_beacon_flag = false;

	while (ad->len > 1) {
		uint8_t field_len = net_buf_simple_pull_u8(ad);
		uint8_t type;

		if (field_len == 0 || field_len > ad->len) {
			break;
		}

		type = net_buf_simple_pull_u8(ad);
		field_len--;

		switch (type) {
		case BT_DATA_NAME_COMPLETE:
		case BT_DATA_NAME_SHORTENED: {
			size_t copy_len = MIN(field_len, name_len - 1);
			memcpy(name, ad->data, copy_len);
			name[copy_len] = '\0';
			break;
		}
		case BT_DATA_MANUFACTURER_DATA:
			if (is_ibeacon(ad->data, field_len)) {
				*is_beacon_flag = true;
				if (field_len >= 25) {
					*major = (ad->data[20] << 8) | ad->data[21];
					*minor = (ad->data[22] << 8) | ad->data[23];
					*beacon_tx = (int8_t)ad->data[24];
				}
			}
			break;
		default:
			break;
		}

		net_buf_simple_pull(ad, field_len);
	}
}

static void scan_cb(const bt_addr_le_t *addr, int8_t rssi,
		    uint8_t adv_type, struct net_buf_simple *ad)
{
	struct scan_msg msg = {0};

	/* Filtra duplicados por software */
	if (seen_devices_check_add(addr)) {
		return;
	}

	/* Copia o ad data para parsing */
	struct net_buf_simple ad_copy;
	uint8_t ad_buf[64];
	size_t copy_len = MIN(ad->len, sizeof(ad_buf));

	memcpy(ad_buf, ad->data, copy_len);
	net_buf_simple_init_with_data(&ad_copy, ad_buf, copy_len);

	parse_ad_data(&ad_copy, msg.name, sizeof(msg.name), &msg.is_beacon,
		      &msg.major, &msg.minor, &msg.beacon_tx);

	bt_addr_le_copy(&msg.addr, addr);
	msg.rssi = rssi;

	/* Enfileira sem bloquear (descarta se fila cheia) */
	k_msgq_put(&scan_msgq, &msg, K_NO_WAIT);
}

/* ---------- Thread de impressao ---------- */
static void print_thread_fn(void *p1, void *p2, void *p3)
{
	ARG_UNUSED(p1);
	ARG_UNUSED(p2);
	ARG_UNUSED(p3);

	struct scan_msg msg;
	char addr_str[BT_ADDR_STR_LEN];

	for (;;) {
		if (k_msgq_get(&scan_msgq, &msg, K_FOREVER) == 0) {
			devices_found++;

			dk_set_led_on(FOUND_LED);

			bt_addr_to_str(&msg.addr.a, addr_str, sizeof(addr_str));

			if (msg.is_beacon) {
				uart_send("[%3d] %s (%s) RSSI:%d dBm ** iBEACON ** Major:%u Minor:%u TxPwr:%d\r\n",
					  devices_found, addr_str,
					  addr_type_str(msg.addr.type),
					  msg.rssi, msg.major, msg.minor,
					  msg.beacon_tx);
			} else if (msg.name[0] != '\0') {
				uart_send("[%3d] %s (%s) RSSI:%d dBm  \"%s\"\r\n",
					  devices_found, addr_str,
					  addr_type_str(msg.addr.type),
					  msg.rssi, msg.name);
			} else {
				uart_send("[%3d] %s (%s) RSSI:%d dBm\r\n",
					  devices_found, addr_str,
					  addr_type_str(msg.addr.type),
					  msg.rssi);
			}

			dk_set_led_off(FOUND_LED);
		}
	}
}

K_THREAD_DEFINE(print_thread_id, PRINT_THREAD_STACK,
		print_thread_fn, NULL, NULL, NULL,
		PRINT_THREAD_PRIO, 0, 0);

static int scanner_start(void)
{
	int err;
	struct bt_le_scan_param scan_param = {
		.type     = BT_LE_SCAN_TYPE_PASSIVE,
		.options  = BT_LE_SCAN_OPT_FILTER_DUPLICATE,
		.interval = SCAN_INTERVAL,
		.window   = SCAN_WINDOW,
	};

	if (scanner_active) {
		return 0;
	}

	devices_found = 0;
	seen_devices_clear();

	err = bt_le_scan_start(&scan_param, scan_cb);
	if (err) {
		LOG_ERR("Erro ao iniciar scanner: %d", err);
		return err;
	}

	scanner_active = true;
	dk_set_led_on(SCANNER_STATUS_LED);
	dk_set_led_off(BEACON_STATUS_LED);

	uart_send("\r\n========= MODO SCANNER ==========\r\n");
	uart_send("Escaneando dispositivos BLE...\r\n");
	uart_send("Intervalo: %d ms  Janela: %d ms\r\n",
		  (SCAN_INTERVAL * 625) / 1000, (SCAN_WINDOW * 625) / 1000);
	uart_send("Filtro: duplicados removidos\r\n");
	uart_send("==================================\r\n\r\n");

	LOG_INF("Scanner iniciado");
	return 0;
}

static int scanner_stop(void)
{
	int err;

	if (!scanner_active) {
		return 0;
	}

	err = bt_le_scan_stop();
	if (err) {
		LOG_ERR("Erro ao parar scanner: %d", err);
		return err;
	}

	scanner_active = false;
	dk_set_led_off(SCANNER_STATUS_LED);
	dk_set_led_off(FOUND_LED);

	uart_send("\r\n--- Scan parado. %d dispositivos encontrados ---\r\n\r\n",
		  devices_found);

	LOG_INF("Scanner parado (%d dispositivos)", devices_found);
	return 0;
}

/* ---------- Botoes ---------- */
static void button_handler(uint32_t button_state, uint32_t has_changed)
{
	/* Botao 1: Alterna entre Beacon e Scanner */
	if (has_changed & DK_BTN1_MSK && button_state & DK_BTN1_MSK) {
		/* Para o modo atual */
		if (current_mode == MODE_BEACON) {
			beacon_stop();
			current_mode = MODE_SCANNER;
			scanner_start();
		} else {
			scanner_stop();
			current_mode = MODE_BEACON;
			beacon_start();
		}
		LOG_INF("Modo alterado para: %s",
			current_mode == MODE_BEACON ? "BEACON" : "SCANNER");
	}

	/* Botao 2: Acao contextual */
	if (has_changed & DK_BTN2_MSK && button_state & DK_BTN2_MSK) {
		if (current_mode == MODE_SCANNER) {
			/* Inicia/para scan */
			if (scanner_active) {
				scanner_stop();
			} else {
				scanner_start();
			}
		} else {
			/* Incrementa Minor do beacon */
			beacon_stop();
			beacon_minor++;
			if (beacon_minor > 0xFFFF) {
				beacon_minor = 1;
				beacon_major++;
			}
			uart_send("Beacon atualizado: Major=%u Minor=%u\r\n",
				  beacon_major, beacon_minor);
			beacon_start();
		}
	}

	/* Botao 3: No modo scanner, alterna scan passivo/ativo */
	if (has_changed & DK_BTN3_MSK && button_state & DK_BTN3_MSK) {
		if (current_mode == MODE_SCANNER && !scanner_active) {
			/* Reseta contador e inicia novo scan */
			devices_found = 0;
			uart_send("\r\n--- Contador resetado ---\r\n");
			scanner_start();
		}
	}

	/* Botao 4: Mostra status */
	if (has_changed & DK_BTN4_MSK && button_state & DK_BTN4_MSK) {
		uart_send("\r\n------ STATUS ------\r\n");
		uart_send("Modo: %s\r\n",
			  current_mode == MODE_BEACON ? "BEACON" : "SCANNER");
		if (current_mode == MODE_BEACON) {
			uart_send("Beacon: %s\r\n", beacon_active ? "ATIVO" : "PARADO");
			uart_send("Major: %u  Minor: %u\r\n", beacon_major, beacon_minor);
		} else {
			uart_send("Scanner: %s\r\n", scanner_active ? "ATIVO" : "PARADO");
			uart_send("Dispositivos encontrados: %d\r\n", devices_found);
		}
		uart_send("--------------------\r\n\r\n");
	}
}

/* ---------- Main ---------- */
int main(void)
{
	int err;
	int blink_status = 0;

	/* Inicializa LEDs e Botoes */
	err = dk_leds_init();
	if (err) {
		LOG_ERR("Erro ao inicializar LEDs: %d", err);
	}

	err = dk_buttons_init(button_handler);
	if (err) {
		LOG_ERR("Erro ao inicializar botoes: %d", err);
	}

	/* Inicializa UART */
	err = uart_init();
	if (err) {
		LOG_ERR("Erro ao inicializar UART: %d", err);
		return -1;
	}

	/* Mensagem de boas-vindas */
	uart_send("\r\n==========================================\r\n");
	uart_send("  nRF52840 BLE Beacon + Scanner\r\n");
	uart_send("==========================================\r\n");
	uart_send("Botao 1: Alterna Beacon <-> Scanner\r\n");
	uart_send("Botao 2: Start/Stop scan | Muda Minor\r\n");
	uart_send("Botao 3: Reset scan + novo scan\r\n");
	uart_send("Botao 4: Mostra status\r\n");
	uart_send("==========================================\r\n\r\n");

	/* Inicializa Bluetooth */
	err = bt_enable(NULL);
	if (err) {
		LOG_ERR("Erro ao inicializar Bluetooth: %d", err);
		return -1;
	}
	LOG_INF("Bluetooth inicializado");

	/* Inicia no modo Beacon */
	uart_send("Iniciando modo Beacon...\r\n\r\n");
	beacon_start();

	/* Loop principal - pisca LED1 */
	for (;;) {
		dk_set_led(RUN_STATUS_LED, (++blink_status) % 2);
		k_sleep(K_MSEC(LED_BLINK_INTERVAL));
	}

	return 0;
}

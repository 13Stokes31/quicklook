#pragma once
#include <cstddef>
#include <cstdint>

struct QuickLookBackend;
struct ImageRect { uint32_t x, y, width, height; };

extern "C" {
QuickLookBackend *ql_create(const uint8_t *path, size_t length);
void ql_destroy(QuickLookBackend *backend);
bool ql_poll(QuickLookBackend *backend);
bool ql_navigate(QuickLookBackend *backend, bool next);
uint32_t ql_state(const QuickLookBackend *backend);
// 0: imagen, 1: texto, 2: PDF. La ruta conserva la codificación nativa de Unix.
uint32_t ql_kind(const QuickLookBackend *backend);
const char *ql_path(const QuickLookBackend *backend);
const char *ql_title(const QuickLookBackend *backend);
uint32_t ql_background(const QuickLookBackend *backend);
// Buffer RGB32 de rect->width * rect->height píxeles, sin el marco.
// Prestado: válido hasta la siguiente mutación del backend, solo en el hilo GUI.
const uint32_t *ql_render(QuickLookBackend *backend, uint32_t width, uint32_t height, ImageRect *rect);
}

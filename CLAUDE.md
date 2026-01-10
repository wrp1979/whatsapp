# WhatSie - WhatsApp Desktop Client

Fork do Whatsie com melhorias de performance e correcoes de bugs.

## Build

**IMPORTANTE**: Sempre use o script de build para compilar. Nunca use `make` diretamente.

```bash
# Compilar (incrementa build number automaticamente)
./build.sh

# Limpar e recompilar do zero
./build.sh rebuild

# Executar o app
./build.sh run

# Ver versao atual
./build.sh version

# Apenas limpar
./build.sh clean
```

O script `build.sh` automaticamente:
1. Incrementa o numero do build em `src/BUILD_NUMBER`
2. Roda qmake para gerar o Makefile atualizado
3. Compila com todos os cores disponiveis
4. Mostra a versao final

## Estrutura do Projeto

```
src/
  BUILD_NUMBER     # Numero do build atual (auto-incrementado)
  WhatsApp.pro     # Arquivo de projeto Qt
  webenginepage.cpp # Logica do WebEngine (focus keeper, injecao JS)
  mainwindow.cpp   # Janela principal
  ...
build/
  whatsie          # Binario compilado
```

## Debugging

### Console do WhatsApp Web
O app injeta JavaScript no WhatsApp Web. Para debug:

```javascript
// No console do DevTools (F12 no app):
window._whatsieFocusControl.status()   // Status do focus keeper
window._whatsieFocusControl.disable()  // Desabilitar temporariamente
window._whatsieFocusControl.enable()   // Reabilitar
```

### Build Info
```bash
./build/whatsie --build-info
# Mostra: version: 4.16.3 (build N), branch: main, commit: abc123
```

## Arquivos Importantes

- `src/webenginepage.cpp`: Injecao de JavaScript, focus keeper, handlers
- `src/mainwindow.cpp`: Janela principal, tray, shortcuts
- `src/settingswidget.cpp`: Configuracoes do app
- `build.sh`: Script de build com auto-incremento

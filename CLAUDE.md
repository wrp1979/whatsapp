# WhatSie - WhatsApp Desktop Client

---

**PERSONAL FORK - INPUT FOCUS FIX**

This is my personal fork of Whatsie. The main goal is to fix the extremely annoying input focus bug in WhatsApp Web where the message input field constantly loses focus, making it impossible to type properly.

This is a work in progress. I'm actively fixing this issue day by day until it works reliably.

**The problem:** WhatsApp Web loses focus on the message input field randomly, especially after clicking anywhere in the chat area, closing popups, or switching windows.

**My solution:** Aggressive JavaScript injection that forces focus back to the input field (see `src/webenginepage.cpp` - Focus Keeper v4).

---

## Build

**CRITICO - LEIA ANTES DE COMPILAR**:
- **SEMPRE** use `./build.sh` para compilar
- **NUNCA** use `make` diretamente (o BUILD_NUM nao atualiza)
- **NUNCA** use `qmake` diretamente
- Apos modificar codigo, rode `./build.sh` e depois `./build.sh run` para testar

```bash
# Compilar (incrementa build number automaticamente)
./build.sh

# Executar o app apos compilar
./build.sh run

# Compilar E executar em sequencia
./build.sh && ./build.sh run

# Limpar e recompilar do zero (se tiver problemas)
./build.sh rebuild

# Ver versao atual
./build.sh version
```

O script `build.sh` automaticamente:
1. Incrementa o numero do build em `src/BUILD_NUMBER`
2. Deleta main.o e mainwindow.o para forcar recompilacao com novo BUILD_NUM
3. Roda qmake para gerar o Makefile atualizado
4. Compila com todos os cores disponiveis (-j32)
5. Mostra a versao final

**Se o build number nao atualizar na janela**: use `./build.sh rebuild` para forcar recompilacao total.

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

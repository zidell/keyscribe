# KeyScribe

[![CI](https://github.com/zidell/keyscribe/actions/workflows/ci.yml/badge.svg)](https://github.com/zidell/keyscribe/actions/workflows/ci.yml) [![License: MIT](https://img.shields.io/badge/License-MIT-yellow.svg)](LICENSE) [![codecov](https://codecov.io/gh/zidell/keyscribe/graph/badge.svg)](https://codecov.io/gh/zidell/keyscribe)

[English](README.md) | [한국어](README.ko.md) | [中文](README.zh-CN.md) | [日本語](README.ja.md) | [Español](README.es.md)

KeyScribe es una aplicación de dictado que transcribe las grabaciones del micrófono y pega el texto resultante en el campo activo. Mantiene deliberadamente un conjunto pequeño de funciones y está implementada de forma nativa, con un objetivo de memoria en reposo de unos 20 MB. No incluye un modelo propio, por lo que necesita una clave de API de OpenAI, ElevenLabs o Groq (Groq puede usarse gratis).

## Funciones

- Admite pulsar para hablar, grabación conmutada, Escape para cancelar y límite de tiempo de grabación.
- Muestra el estado de grabación y transcripción, además del nivel de entrada en directo, en un pequeño widget situado por defecto en la parte central inferior de la pantalla, o bien oculto por completo.
- Aunque ocultes el widget, el icono de la barra de menús (bandeja) se vuelve rojo mientras grabas y naranja mientras transcribe.
- Permite cambiar el atajo, el idioma, el modelo, el volumen del sonido, la posición del widget de grabación y la pulsación automática de Enter en Configuración.
- Las **palabras de reconocimiento** mejoran la precisión con los nombres propios, y las **palabras de sustitución** (`buscar => reemplazar`) corrigen errores persistentes justo antes de pegar. Si escribes `[enter]` o `[cmd+k]` en la sustitución, esa tecla se pulsa de verdad en ese punto.
- **Ver registros** en el menú abre los registros de diagnóstico de las últimas 24 horas. Las claves de API y el contenido grabado o transcrito nunca se escriben en ellos.

## Por qué lo hice

Ya hay muchas aplicaciones de dictado. Tras probar varias opciones gratuitas y de pago, y leer experiencias de usuarios en Reddit, comprobé que los modelos STT locales aún no alcanzaban la precisión deseada. Para el uso diario, las API STT de OpenAI y ElevenLabs parecían las opciones más precisas y fiables. Las aplicaciones de pago a menudo costaban demasiado para lo que ofrecían, requerían correcciones frecuentes por exceso de funciones o consumían más de 100 MB de memoria en reposo. Algunas gratuitas no cumplían las expectativas de acabado o calidad de distribución.

## Vista previa

Las siguientes imágenes muestran el menú de control y el flujo de uso de la aplicación.

### Menú de control

| macOS | Windows |
| --- | --- |
| ![Menú de KeyScribe abierto desde la barra de menús de macOS](assets/screenshots/macos-menu-preview.png) | ![Menú de KeyScribe abierto desde la bandeja de Windows](assets/screenshots/windows-menu-preview.png) |

### Flujo de uso

| macOS | Windows |
| --- | --- |
| ![KeyScribe grabando, transcribiendo y pegando automáticamente en macOS](assets/screenshots/keyscribe-flow.gif) | ![KeyScribe grabando, transcribiendo y pegando automáticamente en Windows](assets/screenshots/keyscribe-windows-flow.gif) |

## Instalación

Consulta la [página de descargas de KeyScribe](https://keyscribe.gitools.net) para las descargas e instrucciones de instalación. Todos los instaladores se distribuyen directamente mediante [GitHub Releases](https://github.com/zidell/keyscribe/releases).

| Sistema operativo | Archivo | Instalar o ejecutar |
| --- | --- | --- |
| macOS Apple Silicon | `KeyScribe-macos-arm64-*.dmg` | Abre el DMG y copia la aplicación a Aplicaciones |
| macOS Intel | `KeyScribe-macos-x64-*.dmg` | Abre el DMG y copia la aplicación a Aplicaciones |
| Windows 10/11 x64 | `KeyScribe-windows-x64-*.msix` | Descarga el instalador firmado por SignPath desde GitHub Releases |

## Primer uso

1. Abre **Configuración...** desde el icono de KeyScribe en la barra de menús o la bandeja, y elige una clave y un modelo de ElevenLabs, OpenAI o Groq. El modelo predeterminado de Groq es `whisper-large-v3-turbo`.
2. En macOS, permite los permisos de Micrófono y **Ajustes del Sistema → Privacidad y seguridad → Accesibilidad**. En Windows, permite el acceso al micrófono para aplicaciones de escritorio en **Configuración → Privacidad y seguridad → Micrófono**.
3. Coloca el cursor en el campo donde quieras escribir y mantén pulsada la tecla Command derecha (macOS) o Alt derecha (Windows) mientras hablas. Suelta la tecla para pegar la transcripción.

En Configuración puedes cambiar el atajo, el método para detener la grabación, el límite de tiempo (10, 20, 30 o 60 minutos; 30 de forma predeterminada), el idioma de reconocimiento, el volumen del sonido de inicio y el envío automático. Cuando está activado el envío automático, KeyScribe pulsa Enter después de pegar. Para escribir automáticamente en una aplicación de Windows ejecutada como administrador, es posible que KeyScribe también deba ejecutarse como administrador.

## Proveedores de STT

El proveedor se selecciona automáticamente según el prefijo de la clave de API. El último modelo elegido se guarda por separado para cada proveedor.

| Proveedor | Prefijo de clave de API | Modelo predeterminado |
| --- | --- | --- |
| OpenAI | `sk-` | `gpt-transcribe` |
| ElevenLabs | `sk_` | `scribe_v2` |
| Groq | `gsk_` | `whisper-large-v3-turbo` |

Groq usa una API de transcripción compatible con OpenAI. Obtén una clave mediante **Clave de Groq ↗** en Configuración; después de introducirla podrás cargar la lista de modelos.

### Obtener una clave de API

Pega una clave creada siguiendo estos pasos en **Configuración... → Clave de API** de KeyScribe y pulsa **Actualizar**. Trata la clave como una contraseña: no la compartas ni la publiques.

La forma de crear una clave difiere según el servicio. Una suscripción a ChatGPT es independiente de la facturación de OpenAI API, y ElevenLabs requiere un Full Seat para crear una clave de API personal. Groq es la opción más sencilla para empezar gratis, pero su nivel gratuito tiene límites de uso y de velocidad.

#### OpenAI

1. Inicia sesión o crea una cuenta en la [página de claves de API de OpenAI](https://platform.openai.com/api-keys).
2. Configura un método de pago o créditos de API en **Billing** de API Platform. Una suscripción a ChatGPT Plus o Pro por sí sola no incluye uso de API.
3. Elige el proyecto que usarás. Los usuarios individuales pueden conservar el proyecto predeterminado.
4. Pulsa **Create new secret key**, asígnale un nombre y crea la clave.
5. Copia la clave `sk-…` mostrada en KeyScribe.

Las claves de OpenAI API se crean por proyecto; los permisos y límites de uso se administran en los ajustes del proyecto. [Guía oficial de OpenAI](https://help.openai.com/en/articles/9186755)

#### ElevenLabs

1. Inicia sesión o crea una cuenta en la [página de claves de API de ElevenLabs](https://elevenlabs.io/app/developers/api-keys).
2. Confirma que dispones del **Full Seat** necesario para crear una clave de API personal. De lo contrario, necesitarás el plan o ajuste del administrador del espacio de trabajo correspondiente.
3. Crea una clave en la lista de claves de API personales y asígnale un nombre reconocible.
4. Copia la clave `sk_…` resultante en KeyScribe.

Las claves personales se pueden crear y renovar en los ajustes de claves de API personales. [Guía oficial de ElevenLabs](https://elevenlabs.io/docs/overview/administration/workspaces/api-keys)

#### Groq

1. Inicia sesión o crea una cuenta en la [página de claves de API de Groq Console](https://console.groq.com/keys). Se pueden crear claves en el nivel gratuito.
2. Si es la primera vez, crea un proyecto o selecciona el predeterminado en el selector de proyectos.
3. Pulsa **Create API Key**, asígnale un nombre y crea la clave.
4. Copia la clave `gsk_…` resultante en KeyScribe.

Las claves de Groq pertenecen al proyecto seleccionado y el nivel gratuito tiene límites de solicitudes y procesamiento de audio. Para pasar al nivel Developer de pago con límites mayores se necesita un método de pago. [Guía de proyectos de Groq](https://console.groq.com/docs/projects), [guía de facturación](https://console.groq.com/docs/billing-faqs)

## Solución de problemas

Puedes consultar el estado y los errores en el menú de la barra de menús o de la bandeja. **Ver registros** abre el registro de diagnóstico de la aplicación.

- macOS: `~/Library/Application Support/keyscribe/debug.log`
- Windows: `%APPDATA%\keyscribe\debug.log`
- Ejecuciones de desarrollo: `dist-native/macos-debug.log`, `dist-native/windows-debug.log`

Los registros de la aplicación de inicio de sesión de macOS y de la compilación con vigilancia de fuentes se guardan en `dist-native/app.log` y `dist-native/watch.log`. El [aviso de privacidad](docs/privacy.md) explica cómo se envía el audio y se almacenan las claves de API. El código fuente está disponible bajo la [licencia MIT](LICENSE).

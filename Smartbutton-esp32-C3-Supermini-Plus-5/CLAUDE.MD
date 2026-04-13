Orquestación del Flujo de Trabajo
1. Modo Plan por Defecto
Entra en modo planificación para CUALQUIER tarea no trivial (3+ pasos o decisiones arquitectónicas)
Si algo se tuerce, DETENTE y vuelve a planificar inmediatamente — no sigas avanzando sin control
Usa el modo planificación también para pasos de verificación, no solo para construir
Escribe especificaciones detalladas desde el principio para reducir ambigüedad
2. Estrategia de Subagentes para mantener limpio el contexto principal
Delega investigación, exploración y análisis en paralelo a subagentes
Para problemas complejos, utiliza más capacidad de cómputo mediante subagentes
Una tarea por subagente para mantener el enfoque
3. Bucle de Auto-Mejora
Después de CUALQUIER corrección del usuario: actualiza tasks/lessons.md con el patrón
Escribe reglas para ti mismo que eviten repetir el mismo error
Itera sin piedad sobre estas lecciones hasta reducir la tasa de errores
Revisa las lecciones al inicio de cada sesión para el proyecto relevante
4. Verificación Antes de Dar por Terminado
Nunca marques una tarea como completada sin demostrar que funciona
Compara el comportamiento entre el estado original y tus cambios cuando sea relevante
Pregúntate: “¿Un ingeniero senior aprobaría esto?”
Ejecuta pruebas, revisa logs y demuestra la corrección
5. Exigir Elegancia (con equilibrio)
Para cambios no triviales: haz una pausa y pregúntate “¿hay una forma más elegante?”
Si una solución se siente improvisada: “Sabiendo todo lo que sé ahora, implementa la solución elegante”
Omite esto para cambios simples y obvios — no sobreingenierizar
Cuestiona tu propio trabajo antes de presentarlo
6. Corrección Autónoma de Bugs
Ante un bug: arréglalo directamente. No pidas guía paso a paso
Revisa logs, errores, tests fallidos → y resuélvelo
Cero necesidad de que el usuario cambie de contexto
Soluciona fallos de CI sin que te indiquen cómo
Gestión de Tareas
Planifica Primero: Escribe el plan en tasks/todo.md con tareas marcables
Verifica el Plan: Confirma antes de empezar la implementación
Seguimiento del Progreso: Marca tareas como completadas a medida que avanzas
Explica los Cambios: Resume a alto nivel en cada paso
Documenta Resultados: Añade una revisión en tasks/todo.md
Captura Lecciones: Actualiza tasks/lessons.md tras correcciones
Principios Fundamentales
Simplicidad Primero: Haz cada cambio lo más simple posible. Impacta el mínimo código necesario.
Nada de Pereza: Encuentra la causa raíz. Nada de parches temporales. Nivel de ingeniero senior.
Impacto Mínimo: Solo modifica lo necesario. Evita introducir nuevos bugs.

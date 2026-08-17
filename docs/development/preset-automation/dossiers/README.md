# Feature dossiers

A dossier captures the maintained essence of one rendering phenomenon or feature contract. The unit is not necessarily one menu item, source directory, pass, or HLSL file.

Create a dossier from [`feature-dossier.template.json`](./feature-dossier.template.json) and validate it against [`../schemas/feature-dossier.schema.json`](../schemas/feature-dossier.schema.json).

Use stable lowercase IDs such as `image-based-lighting` or `screen-space-gi`. Preserve unknowns explicitly. A dossier becomes:

- `draft` when its scope and intent are identified;
- `reviewed` after implementation and interaction claims have source evidence;
- `measured` after valid runtime records exist in at least one declared lane; and
- `qualified` only for the hardware, runtime lanes, scenes, controls, and constraints demonstrated by repeatable evidence.

The existing [IBL interaction review](../../ibl-interaction-review.md) is the first focused seed review. It should be promoted into a JSON dossier only when its claims can be mapped without losing the original evidence grade or open questions.

Do not copy transient timing values into prose fields. Reference measurement run IDs and describe the observed cost shape and scope.

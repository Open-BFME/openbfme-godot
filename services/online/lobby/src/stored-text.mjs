// Storage predates Unicode intake validation. Repair decoded values, not JSON's escaped wire text.
export function repairStoredText(value) {
  if (typeof value === 'string') return value.toWellFormed();
  if (!value || typeof value !== 'object') return value;
  let changed = false;
  const entries = Object.entries(value).map(([key, item]) => {
    const cleanKey = key.toWellFormed();
    const cleanItem = repairStoredText(item);
    if (cleanKey !== key || cleanItem !== item) changed = true;
    return [cleanKey, cleanItem];
  });
  if (!changed) return value;
  return Array.isArray(value) ? entries.map(([, item]) => item) : Object.fromEntries(entries);
}

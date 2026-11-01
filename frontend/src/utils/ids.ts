/** Compare decimal integer strings without converting them to numbers. */
export function compareIds(left: string, right: string): number {
  return left.length - right.length || left.localeCompare(right)
}

/** Merge records by decimal id, keeping the later record and ordering the result by id. */
export function mergeById<T extends { id: string }>(current: T[], incoming: T[]): T[] {
  const byId = new Map(current.map((record) => [record.id, record]))
  for (const record of incoming) {
    byId.set(record.id, record)
  }
  return [...byId.values()].sort((left, right) => compareIds(left.id, right.id))
}

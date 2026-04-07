/**
 * Minimal type-safe pub/sub – zero dependency.
 *
 * Usage:
 *   const bus = new PubSub<{ tick: number; reset: void }>();
 *   const unsub = bus.on('tick', elapsed => console.log(elapsed));
 *   bus.emit('tick', 1234);
 *   unsub();                       // unsubscribe
 *   bus.once('reset', () => {});   // single-shot
 */

type Listener<T> = (payload: T) => void;

export class PubSub<EventMap extends Record<string, unknown>> {
    private _subs = new Map<keyof EventMap, Set<Listener<never>>>();

    on<K extends keyof EventMap>(event: K, fn: Listener<EventMap[K]>): () => void {
        let set = this._subs.get(event);
        if (!set) {
            set = new Set();
            this._subs.set(event, set);
        }
        set.add(fn as Listener<never>);
        return () => { set!.delete(fn as Listener<never>); };
    }

    once<K extends keyof EventMap>(event: K, fn: Listener<EventMap[K]>): () => void {
        const unsub = this.on(event, (payload) => {
            unsub();
            fn(payload);
        });
        return unsub;
    }

    emit<K extends keyof EventMap>(event: K, payload: EventMap[K]): void {
        const set = this._subs.get(event);
        if (!set) return;
        // iterate a snapshot so listeners can safely unsubscribe mid-emit
        for (const fn of [...set]) {
            (fn as Listener<EventMap[K]>)(payload);
        }
    }

    clear(): void {
        this._subs.clear();
    }
}
